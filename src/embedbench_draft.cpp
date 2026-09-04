// EmbedBench draft core implementation. See embedbench_draft.h: this is
// an experimental candidate whose behavior comes from measured winners in
// the experiment ledger; rework is expected and nothing here is final.
#include "embedbench_draft.h"
#include "embedbench_device.h"

#include <Arduino.h>
#include <HostBus.h>
#include <HostClock.h>
#include <HostInterrupt.h>
#include <HostUart.h>
#include <SPI.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ebd {
namespace {

constexpr size_t kCapacity = 64;
constexpr size_t kMaxWireDevices = 2;
constexpr size_t kMaxFormats = 8;
constexpr uint32_t kMaxFrameBits = 64;
constexpr size_t kDeferralCapacity = 4;

// An effect raised while a device method was on the stack, held until
// the outermost device call has completed (re-entrancy contract).
struct Deferred {
  uint8_t kind;  // 0 = interrupt on `pin`, 1 = frame to the app receiver
  uint8_t pin;
  uint8_t bus;
  uint16_t format;
  uint8_t bits;
  uint8_t data[8];
};

struct FormatSlot {
  bool used = false;
  char name[20] = {0};
  uint32_t schema = 0;
};

struct WireDeviceSlot {
  bool used = false;
  uint16_t address = 0;
  WireDeviceOps ops = {nullptr, nullptr, nullptr};
};

struct State {
  // Trace.
  Event buf[kCapacity];
  size_t count = 0;
  uint32_t nextSeq = 1;
  uint32_t dropped = 0;
  uint32_t diagCount = 0;
  bool running = false;

  // Clock (X4 split, X8 defer, X7 zero-wait guard).
  uint32_t tickUs = 1000;
  uint64_t vnow = 0;
  uint64_t nextTick = 1000;
  uint32_t ticks = 0;
  uint32_t pendingTicks = 0;
  uint32_t lateTicks = 0;
  uint32_t zeroWaits = 0;
  uint32_t zeroInDirector = 0;

  // Context tracking.
  uint32_t dirDepth = 0;
  uint32_t isrDepth = 0;

  // Bindings (persist across runs).
  WireDeviceSlot wireDevices[kMaxWireDevices];
  UartTxHandler uartHandler = nullptr;
  void* uartUser = nullptr;
  ChannelHandler channelHandler = nullptr;
  void* channelUser = nullptr;
  SpiTransferFn spiHandler = nullptr;
  void* spiUser = nullptr;
  PinWriteForward pinForward = nullptr;
  void* pinForwardUser = nullptr;
  FrameHandler frameDevice = nullptr;
  void* frameDeviceUser = nullptr;
  FrameHandler frameReceiver = nullptr;
  void* frameReceiverUser = nullptr;
  FormatSlot formats[kMaxFormats];
  bool inSpiTransaction = false;
  uint32_t spiBulkCount = 0;
  uint8_t spiMosiSum = 0;
  uint8_t spiMisoSum = 0;
  TickHandler tickHandler = nullptr;
  void* tickUser = nullptr;
  ZeroWaitHandler zeroHandler = nullptr;
  void* zeroUser = nullptr;

  // Re-entrancy contract: effects raised while a device method is on the
  // stack are delivered after the outermost device call has completed.
  uint32_t deviceDepth = 0;
  uint32_t maxDeviceDepth = 0;
  Deferred deferred[kDeferralCapacity];
  size_t deferredCount = 0;
  uint32_t deferredIsrs = 0;
  uint32_t deferredFrames = 0;
  uint32_t deferredDropped = 0;

  // I2C is one bus: the address whose last transfer ended without STOP,
  // or 0xFFFF. Any transfer to another address closes the sequence.
  uint16_t i2cOpenAddress = 0xFFFF;

  TickDeviceFn tickDevice = nullptr;
  void* tickDeviceUser = nullptr;
};

State state;

uint8_t currentCtx() {
  if (state.isrDepth > 0) return 2;
  if (state.dirDepth > 0) return 1;
  return 0;
}

const char* ctxName(uint8_t ctx) {
  switch (ctx) {
    case 1:
      return "tick";
    case 2:
      return "isr";
    default:
      return "main";
  }
}

const char* originName(Origin origin) {
  switch (origin) {
    case Origin::kDir:
      return "dir";
    case Origin::kDev:
      return "dev";
    case Origin::kCore:
      return "core";
    case Origin::kDiag:
      return "diag";
    default:
      return "app";
  }
}

uint32_t vrecord(Origin origin, uint32_t link, const char* fmt, va_list ap) {
  const uint32_t seq = state.nextSeq++;
  if (!state.running || state.count >= kCapacity) {
    if (state.running) ++state.dropped;
    return seq;
  }
  Event& e = state.buf[state.count++];
  e.seq = seq;
  e.timeUs = state.vnow;
  e.ctx = currentCtx();
  e.origin = origin;
  e.link = link;
  vsnprintf(e.text, sizeof(e.text), fmt, ap);
  return seq;
}

uint32_t recordf(Origin origin, uint32_t link, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const uint32_t seq = vrecord(origin, link, fmt, ap);
  va_end(ap);
  return seq;
}

void hexOf(const uint8_t* data, size_t len, char* out, size_t cap) {
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 3 <= cap; ++i) {
    pos += snprintf(out + pos, cap - pos, "%02X", data[i]);
  }
  if (pos == 0 && cap > 0) out[0] = '\0';
}

// Payloads up to four bytes print as hex; larger ones are summarized as
// length plus checksum so bulk frames stay one readable line (SCOPE 3.2).
void payloadLabel(const uint8_t* data, size_t bytes, char* out, size_t cap) {
  if (bytes == 0) {
    snprintf(out, cap, "empty");
    return;
  }
  if (bytes <= 4) {
    char hex[12];
    hexOf(data, bytes, hex, sizeof(hex));
    snprintf(out, cap, "data=%s", hex);
  } else {
    uint8_t sum = 0;
    for (size_t i = 0; i < bytes; ++i) sum = static_cast<uint8_t>(sum + data[i]);
    snprintf(out, cap, "len=%u sum=%02X", static_cast<unsigned>(bytes), sum);
  }
}

// Serial bytes print as text when every byte is printable ASCII, and as a
// binary payload label otherwise, so NUL or high bytes never cut a line.
void bytesLabel(const uint8_t* data, size_t len, char* out, size_t cap) {
  bool printable = len > 0;
  for (size_t i = 0; i < len && printable; ++i) {
    if (data[i] < 0x20 || data[i] > 0x7E) printable = false;
  }
  if (printable) {
    snprintf(out, cap, "%.*s", static_cast<int>(len),
             reinterpret_cast<const char*>(data));
  } else {
    payloadLabel(data, len, out, cap);
  }
}

WireDeviceSlot* findWireDevice(uint16_t address) {
  for (size_t i = 0; i < kMaxWireDevices; ++i) {
    if (state.wireDevices[i].used && state.wireDevices[i].address == address) {
      return &state.wireDevices[i];
    }
  }
  return nullptr;
}

// --- Device call bracketing (re-entrancy contract) ------------------------

void enterDevice() {
  ++state.deviceDepth;
  if (state.deviceDepth > state.maxDeviceDepth) {
    state.maxDeviceDepth = state.deviceDepth;
  }
}

void leaveDevice() {
  if (state.deviceDepth > 0) --state.deviceDepth;
}

// Hold an effect raised inside a device call. Beyond the capacity the
// effect is diagnosed and dropped: never delivered re-entrantly, never
// lost in silence.
void queueDeferred(const Deferred& effect) {
  if (state.deferredCount < kDeferralCapacity) {
    state.deferred[state.deferredCount++] = effect;
    if (effect.kind == 0) ++state.deferredIsrs;
    else ++state.deferredFrames;
    return;
  }
  ++state.deferredDropped;
  ++state.diagCount;
  if (effect.kind == 0) {
    recordf(Origin::kDiag, 0, "diag.deferred_full kind=isr pin=%u", effect.pin);
  } else {
    recordf(Origin::kDiag, 0, "diag.deferred_full kind=frame bus=%u", effect.bus);
  }
}

// Called once the operation that ran a device is fully recorded: deliver
// the held effects in order, each as a fresh top-level call chain.
void deliverDeferred() {
  while (state.deviceDepth == 0 && state.deferredCount > 0) {
    const Deferred effect = state.deferred[0];
    for (size_t i = 1; i < state.deferredCount; ++i) {
      state.deferred[i - 1] = state.deferred[i];
    }
    --state.deferredCount;
    if (effect.kind == 0) {
      HostArduino::triggerInterrupt(effect.pin);
    } else if (state.frameReceiver) {
      state.frameReceiver(effect.bus, effect.format, effect.data, effect.bits,
                          state.frameReceiverUser);
    }
  }
}

// --- Clock hooks --------------------------------------------------------

uint64_t onNow(void*) { return state.vnow; }

void fireTick() {
  ++state.ticks;
  ++state.dirDepth;
  if (state.tickDevice) {
    enterDevice();
    state.tickDevice(state.vnow, state.tickDeviceUser);
    leaveDevice();
    deliverDeferred();
  }
  if (state.tickHandler) state.tickHandler(state.ticks, state.tickUser);
  --state.dirDepth;
}

void onWait(uint32_t us, void*) {
  if (us == 0) {
    // The only external-processing opportunity of a busy-waiting sketch
    // (X7); never re-entered from director or ISR context.
    if (state.dirDepth > 0 || state.isrDepth > 0) {
      ++state.zeroInDirector;
      return;
    }
    ++state.zeroWaits;
    if (state.zeroHandler) {
      ++state.dirDepth;
      state.zeroHandler(state.zeroWaits, state.zeroUser);
      --state.dirDepth;
    }
    return;
  }
  if (state.dirDepth > 0 || state.isrDepth > 0) {
    // Nested wait inside a director or ISR: advance time, defer the tick
    // firing to the outer splitter (X8's only clean policy).
    const uint64_t target = state.vnow + us;
    while (state.nextTick <= target) {
      ++state.pendingTicks;
      state.nextTick += state.tickUs;
    }
    state.vnow = target;
    return;
  }
  const uint64_t target = state.vnow + us;
  while (state.nextTick <= target) {
    state.vnow = state.nextTick;
    state.nextTick += state.tickUs;
    fireTick();
    while (state.pendingTicks > 0) {
      --state.pendingTicks;
      ++state.lateTicks;
      fireTick();
    }
  }
  if (target > state.vnow) state.vnow = target;
}

// --- GPIO and interrupt hooks --------------------------------------------

void onPinWrite(uint8_t pin, uint8_t value, void*) {
  recordf(Origin::kApp, 0, "gpio.write pin=%u val=%u", pin, value);
  if (state.pinForward) {
    // The forward routes into a device's lineIn: a device call.
    enterDevice();
    state.pinForward(pin, value, state.pinForwardUser);
    leaveDevice();
    deliverDeferred();
  }
}

int onPinRead(uint8_t pin, uint8_t held, void*) {
  // Held reads have no responder callback, so the event is atomic and a
  // single line suffices (X20 applies only when a callback can re-enter).
  recordf(Origin::kApp, 0, "gpio.read pin=%u val=%u", pin, held);
  return held;
}

void onPinMode(uint8_t pin, uint8_t mode, void*) {
  recordf(Origin::kApp, 0, "gpio.mode pin=%u mode=%u", pin, mode);
}

void onInterruptEvent(HostArduino::InterruptEvent event,
                      const HostArduino::InterruptSlot& slot, void*) {
  switch (event) {
    case HostArduino::kInterruptAttach:
      recordf(Origin::kApp, 0, "int.attach pin=%u trig=%u", slot.pin,
              static_cast<unsigned>(slot.trigger));
      break;
    case HostArduino::kInterruptDetach:
      recordf(Origin::kApp, 0, "int.detach pin=%u", slot.pin);
      break;
    case HostArduino::kInterruptEnter:
      ++state.isrDepth;
      recordf(Origin::kCore, 0, "isr.enter pin=%u", slot.pin);
      break;
    case HostArduino::kInterruptExit:
      recordf(Origin::kCore, 0, "isr.exit pin=%u", slot.pin);
      if (state.isrDepth > 0) --state.isrDepth;
      break;
  }
}

// --- Wire hooks: request/response two-line events (X20 winner) ----------

uint8_t onWireWrite(uint8_t address, const uint8_t* data, size_t len,
                    bool stop, void*) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  WireDeviceSlot* dev = findWireDevice(address);
  const bool continued = dev != nullptr && state.i2cOpenAddress == address;
  const uint32_t req = recordf(Origin::kApp, 0, "i2c.req addr=%02X data=%s stop=%u%s",
                               address, hex, stop ? 1 : 0, continued ? " rs" : "");
  uint8_t status = 2;  // address NACK when no responder is bound (X11)
  if (dev != nullptr) {
    enterDevice();
    status = dev->ops.onWrite(data, len, stop, continued, dev->ops.user);
    leaveDevice();
    if (status > ebdev::kI2cOther) {
      ++state.diagCount;
      recordf(Origin::kDiag, req, "diag.i2c_status addr=%02X status=%u", address,
              status);
    }
  } else {
    ++state.diagCount;
    recordf(Origin::kDiag, req, "diag.unbound addr=%02X", address);
  }
  state.i2cOpenAddress = stop ? 0xFFFF : address;
  recordf(Origin::kDev, req, "i2c.resp status=%u", status);
  deliverDeferred();
  return status;
}

size_t onWireRead(uint8_t address, uint8_t* data, size_t len, bool stop,
                  void*) {
  WireDeviceSlot* dev = findWireDevice(address);
  const bool continued = dev != nullptr && state.i2cOpenAddress == address;
  const uint32_t req = recordf(Origin::kApp, 0, "i2c.rd.req addr=%02X req=%u stop=%u%s",
                               address, static_cast<unsigned>(len), stop ? 1 : 0,
                               continued ? " rs" : "");
  size_t count = 0;
  if (dev != nullptr) {
    enterDevice();
    count = dev->ops.onRead(data, len, stop, continued, dev->ops.user);
    leaveDevice();
    if (count > len) {
      // A model that claims more bytes than the buffer holds would make
      // the master read past it: diagnose and treat as nothing supplied.
      ++state.diagCount;
      recordf(Origin::kDiag, req, "diag.i2c_read_length addr=%02X got=%u max=%u",
              address, static_cast<unsigned>(count), static_cast<unsigned>(len));
      count = 0;
    }
  } else {
    ++state.diagCount;
    recordf(Origin::kDiag, req, "diag.unbound addr=%02X", address);
  }
  state.i2cOpenAddress = stop ? 0xFFFF : address;
  char hex[12];
  hexOf(data, count, hex, sizeof(hex));
  recordf(Origin::kDev, req, "i2c.rd.resp len=%u data=%s",
          static_cast<unsigned>(count), hex);
  deliverDeferred();
  return count;
}

// --- SPI hooks: request/response pair per byte outside a transaction; a
// transaction coalesces its bytes into one summary event (count plus
// checksums), which is the bulk-recording candidate from SCOPE 3.2.

uint8_t onSpiTransfer(uint8_t mosi, void*) {
  if (state.inSpiTransaction) {
    uint8_t miso = 0xFF;
    if (state.spiHandler != nullptr) {
      enterDevice();
      miso = state.spiHandler(mosi, state.spiUser);
      leaveDevice();
    }
    ++state.spiBulkCount;
    state.spiMosiSum = static_cast<uint8_t>(state.spiMosiSum + mosi);
    state.spiMisoSum = static_cast<uint8_t>(state.spiMisoSum + miso);
    return miso;
  }
  const uint32_t req = recordf(Origin::kApp, 0, "spi.req mosi=%02X", mosi);
  uint8_t miso = 0xFF;  // host default: idle bus
  if (state.spiHandler != nullptr) {
    enterDevice();
    miso = state.spiHandler(mosi, state.spiUser);
    leaveDevice();
  } else {
    ++state.diagCount;
    recordf(Origin::kDiag, req, "diag.unbound spi");
  }
  recordf(Origin::kDev, req, "spi.resp miso=%02X", miso);
  deliverDeferred();
  return miso;
}

void onSpiTransaction(bool begin, const SPISettings&, void*) {
  if (begin) {
    state.inSpiTransaction = true;
    state.spiBulkCount = 0;
    state.spiMosiSum = 0;
    state.spiMisoSum = 0;
    recordf(Origin::kApp, 0, "spi.begin");
  } else {
    state.inSpiTransaction = false;
    recordf(Origin::kApp, 0, "spi.bulk n=%u mosi_sum=%02X miso_sum=%02X",
            state.spiBulkCount, state.spiMosiSum, state.spiMisoSum);
    deliverDeferred();
  }
}

// --- UART hook: device replies go through the RX sink (X21) --------------

void onUartActivity(HostUart::ActivityEvent event, HostUart&,
                    const uint8_t* data, size_t len, void*) {
  switch (event) {
    case HostUart::kUartTx: {
      char text[24];
      bytesLabel(data, len, text, sizeof(text));
      recordf(Origin::kApp, 0, "uart.tx %s", text);
      if (state.uartHandler) {
        enterDevice();
        state.uartHandler(data, len, state.uartUser);
        leaveDevice();
        deliverDeferred();
      }
      break;
    }
    case HostUart::kUartRx:
      if (data[0] >= 0x20 && data[0] <= 0x7E) {
        recordf(Origin::kApp, 0, "uart.rx %c", data[0]);
      } else {
        recordf(Origin::kApp, 0, "uart.rx 0x%02X", data[0]);
      }
      break;
    case HostUart::kUartBegin:
      recordf(Origin::kApp, 0, "uart.begin");
      break;
    case HostUart::kUartEnd:
      recordf(Origin::kApp, 0, "uart.end");
      break;
    case HostUart::kUartConfig:
      recordf(Origin::kApp, 0, "uart.config");
      break;
    case HostUart::kUartRxDiscard:
      recordf(Origin::kApp, 0, "uart.rx_discard len=%u",
              static_cast<unsigned>(len));
      break;
  }
}

}  // namespace

// --- Public draft API -----------------------------------------------------

bool bindWireDevice(uint16_t address, const WireDeviceOps& ops) {
  if (findWireDevice(address) != nullptr) {
    ++state.diagCount;
    return false;
  }
  for (size_t i = 0; i < kMaxWireDevices; ++i) {
    if (!state.wireDevices[i].used) {
      state.wireDevices[i].used = true;
      state.wireDevices[i].address = address;
      state.wireDevices[i].ops = ops;
      return true;
    }
  }
  ++state.diagCount;
  return false;
}

void bindUartDevice(UartTxHandler handler, void* user) {
  state.uartHandler = handler;
  state.uartUser = user;
}

void setChannelHandler(ChannelHandler handler, void* user) {
  state.channelHandler = handler;
  state.channelUser = user;
}

void bindSpiDevice(SpiTransferFn handler, void* user) {
  state.spiHandler = handler;
  state.spiUser = user;
}

void setPinWriteForward(PinWriteForward handler, void* user) {
  state.pinForward = handler;
  state.pinForwardUser = user;
}

void bindFrameDevice(FrameHandler handler, void* user) {
  state.frameDevice = handler;
  state.frameDeviceUser = user;
}

void setFrameReceiver(FrameHandler handler, void* user) {
  state.frameReceiver = handler;
  state.frameReceiverUser = user;
}

void setTickHandler(TickHandler handler, void* user) {
  state.tickHandler = handler;
  state.tickUser = user;
}

void bindTickDevice(TickDeviceFn fn, void* user) {
  state.tickDevice = fn;
  state.tickDeviceUser = user;
}

void setZeroWaitHandler(ZeroWaitHandler handler, void* user) {
  state.zeroHandler = handler;
  state.zeroUser = user;
}

void runBegin(uint32_t tickUs) {
  state.count = 0;
  state.nextSeq = 1;
  state.dropped = 0;
  state.diagCount = 0;
  state.tickUs = tickUs;
  state.vnow = 0;
  state.nextTick = tickUs;
  state.ticks = 0;
  state.pendingTicks = 0;
  state.lateTicks = 0;
  state.zeroWaits = 0;
  state.zeroInDirector = 0;
  state.dirDepth = 0;
  state.isrDepth = 0;
  state.inSpiTransaction = false;
  state.spiBulkCount = 0;
  state.spiMosiSum = 0;
  state.spiMisoSum = 0;
  state.deviceDepth = 0;
  state.maxDeviceDepth = 0;
  state.deferredCount = 0;
  state.deferredIsrs = 0;
  state.deferredFrames = 0;
  state.deferredDropped = 0;
  state.i2cOpenAddress = 0xFFFF;
  state.running = true;

  HostArduino::setPinWriteHook(&onPinWrite);
  HostArduino::setPinReadHook(&onPinRead);
  HostArduino::setPinModeHook(&onPinMode);
  HostArduino::setInterruptHook(&onInterruptEvent);
  Wire.setWriteHook(&onWireWrite);
  Wire.setReadHook(&onWireRead);
  SPI.setTransferHook(&onSpiTransfer);
  SPI.setTransactionHook(&onSpiTransaction);
  Serial1.setActivityHook(&onUartActivity);
  HostArduino::setClockHooks(&onNow, &onWait);
}

void runEnd() {
  state.running = false;
  HostArduino::clearClockHooks();
  Serial1.clearActivityHook();
  Wire.clearHooks();
  SPI.clearHooks();
  HostArduino::clearInterruptHook();
  HostArduino::clearPinHooks();
}

void pinInject(Origin origin, uint8_t pin, uint8_t level) {
  const uint8_t previous = HostArduino::pinValue(pin);
  const HostArduino::InterruptTrigger trigger =
      HostArduino::interruptTrigger(pin);
  bool match = false;
  if (previous == LOW && level == HIGH) {
    match = trigger == HostArduino::kTriggerRising ||
            trigger == HostArduino::kTriggerChange;
  } else if (previous == HIGH && level == LOW) {
    match = trigger == HostArduino::kTriggerFalling ||
            trigger == HostArduino::kTriggerChange;
  }
  recordf(origin, 0, "gpio.inject pin=%u %u->%u match=%d", pin, previous,
          level, match ? 1 : 0);
  HostArduino::setPinValue(pin, level);
  if (!match) return;
  if (state.deviceDepth > 0) {
    // A device raised this line from inside one of its methods: the ISR
    // runs after that device call has completed (re-entrancy contract).
    Deferred effect = {0, pin, 0, 0, 0, {0}};
    queueDeferred(effect);
    return;
  }
  HostArduino::triggerInterrupt(pin);
}

bool uartInject(Origin origin, const uint8_t* data, size_t len) {
  char text[24];
  bytesLabel(data, len, text, sizeof(text));
  recordf(origin, 0, "dev.tx %s", text);
  const size_t accepted = Serial1.pushRx(data, len);
  if (accepted < len) {
    ++state.diagCount;
    recordf(Origin::kDiag, 0, "diag.uart_rx_full accepted=%u len=%u",
            static_cast<unsigned>(accepted), static_cast<unsigned>(len));
    return false;
  }
  return true;
}

void chanWrite(Origin origin, uint8_t channel, const uint8_t* data,
               size_t len) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  recordf(origin, 0, "chan.write chan=%u data=%s", channel, hex);
  if (state.channelHandler) {
    enterDevice();
    const bool applied =
        state.channelHandler(channel, data, len, state.channelUser);
    leaveDevice();
    if (!applied) {
      ++state.diagCount;
      recordf(Origin::kDiag, 0, "diag.chan_reject chan=%u len=%u", channel,
              static_cast<unsigned>(len));
    }
    deliverDeferred();
  }
}

// Format labels: a registered id prints its name, an unregistered id
// prints its number, so raw-numbered experiments keep working.
void formatLabel(uint16_t id, char* out, size_t cap) {
  if (id >= 1 && id <= kMaxFormats && state.formats[id - 1].used) {
    snprintf(out, cap, "%s", state.formats[id - 1].name);
  } else {
    snprintf(out, cap, "%u", id);
  }
}

// Atomic acceptance: every refusal is a diagnostic, never a truncation.
bool frameAccept(uint8_t bus, uint16_t format, const uint8_t* data,
                 size_t bits) {
  if (format == 0) {
    ++state.diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_noformat bus=%u", bus);
    return false;
  }
  if (format > kMaxFormats || !state.formats[format - 1].used) {
    // Only ids handed out by registerFormat() are valid: a raw number
    // would bypass the name + schema collision check.
    ++state.diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_unknown_format bus=%u fmt=%u", bus,
            format);
    return false;
  }
  if (bits > kMaxFrameBits) {
    ++state.diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_oversize bus=%u bits=%u max=%u", bus,
            static_cast<unsigned>(bits), kMaxFrameBits);
    return false;
  }
  if (bits > 0 && data == nullptr) {
    ++state.diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_nodata bus=%u bits=%u", bus,
            static_cast<unsigned>(bits));
    return false;
  }
  if (bits > 0 && !ebdev::framePaddingClean(data, bits)) {
    ++state.diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_padding bus=%u bits=%u", bus,
            static_cast<unsigned>(bits));
    return false;
  }
  return true;
}

bool frameTx(Origin origin, uint8_t bus, uint16_t format, const uint8_t* data,
             size_t bits) {
  if (!frameAccept(bus, format, data, bits)) return false;
  char payload[20];
  char label[20];
  payloadLabel(data, ebdev::frameBytes(bits), payload, sizeof(payload));
  formatLabel(format, label, sizeof(label));
  recordf(origin, 0, "frame.tx bus=%u fmt=%s bits=%u %s", bus, label,
          static_cast<unsigned>(bits), payload);
  if (state.frameDevice) {
    enterDevice();
    state.frameDevice(bus, format, data, bits, state.frameDeviceUser);
    leaveDevice();
    deliverDeferred();
  }
  return true;
}

bool frameRx(Origin origin, uint8_t bus, uint16_t format, const uint8_t* data,
             size_t bits) {
  if (!frameAccept(bus, format, data, bits)) return false;
  char payload[20];
  char label[20];
  payloadLabel(data, ebdev::frameBytes(bits), payload, sizeof(payload));
  formatLabel(format, label, sizeof(label));
  recordf(origin, 0, "dev.frame bus=%u fmt=%s bits=%u %s", bus, label,
          static_cast<unsigned>(bits), payload);
  if (state.frameReceiver) {
    if (state.deviceDepth > 0) {
      // Raised from inside a device method: the application receiver
      // runs after that device call has completed (re-entrancy contract).
      Deferred effect = {1, 0, bus, format, static_cast<uint8_t>(bits), {0}};
      const size_t bytes = ebdev::frameBytes(bits);
      for (size_t i = 0; i < bytes && i < sizeof(effect.data); ++i) {
        effect.data[i] = data[i];
      }
      queueDeferred(effect);
    } else {
      state.frameReceiver(bus, format, data, bits, state.frameReceiverUser);
    }
  }
  return true;
}

uint32_t frameCapacityBits() { return kMaxFrameBits; }

size_t deferralCapacity() { return kDeferralCapacity; }

uint16_t registerFormat(const char* name, uint32_t schema) {
  if (name == nullptr || name[0] == '\0') return 0;
  const size_t length = strlen(name);
  if (length > ebdev::kFormatNameMaxLength) {
    // Never truncate: a clipped name could alias another registration.
    ++state.diagCount;
    recordf(Origin::kDiag, 0, "diag.fmt_name_long len=%u",
            static_cast<unsigned>(length));
    return 0;
  }
  for (size_t i = 0; i < kMaxFormats; ++i) {
    if (state.formats[i].used && strcmp(state.formats[i].name, name) == 0) {
      if (state.formats[i].schema != schema) {
        // Same name, different layout: two libraries collided on a name.
        ++state.diagCount;
        recordf(Origin::kDiag, 0, "diag.fmt_conflict name=%s", name);
        return 0;
      }
      return static_cast<uint16_t>(i + 1);
    }
  }
  for (size_t i = 0; i < kMaxFormats; ++i) {
    if (!state.formats[i].used) {
      state.formats[i].used = true;
      state.formats[i].schema = schema;
      snprintf(state.formats[i].name, sizeof(state.formats[i].name), "%s",
               name);
      return static_cast<uint16_t>(i + 1);
    }
  }
  ++state.diagCount;
  recordf(Origin::kDiag, 0, "diag.fmt_full name=%s", name);
  return 0;
}

void dumpf(const char* fmt, ...) {
  char text[50];  // fills the 56-byte event text after the "dump " prefix
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  recordf(Origin::kDir, 0, "dump %s", text);
}

uint64_t nowUs() { return state.vnow; }

Stats stats() {
  Stats s;
  s.events = static_cast<uint32_t>(state.count);
  s.dropped = state.dropped;
  s.zeroWaits = state.zeroWaits;
  s.zeroInDirector = state.zeroInDirector;
  s.lateTicks = state.lateTicks;
  s.ticks = state.ticks;
  s.diagCount = state.diagCount;
  s.deferredIsrs = state.deferredIsrs;
  s.deferredFrames = state.deferredFrames;
  s.deferredDropped = state.deferredDropped;
  s.maxDeviceDepth = state.maxDeviceDepth;
  return s;
}

size_t eventCount() { return state.count; }

size_t respLineCount() {
  size_t count = 0;
  for (size_t i = 0; i < state.count; ++i) {
    if (state.buf[i].link != 0) ++count;
  }
  return count;
}

size_t eventBytes() { return sizeof(Event); }

size_t formatTrace(char* out, size_t cap) {
  size_t pos = 0;
  for (size_t i = 0; i < state.count && pos < cap; ++i) {
    const Event& e = state.buf[i];
    pos += snprintf(out + pos, cap - pos, "%02u %06llu %s %s %s", e.seq,
                    static_cast<unsigned long long>(e.timeUs), ctxName(e.ctx),
                    originName(e.origin), e.text);
    if (e.link != 0 && pos < cap) {
      pos += snprintf(out + pos, cap - pos, " re=%u", e.link);
    }
    if (pos < cap) pos += snprintf(out + pos, cap - pos, "\n");
  }
  return pos;
}

}  // namespace ebd
