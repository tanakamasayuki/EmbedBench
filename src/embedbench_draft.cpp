// EmbedBench draft core implementation. See embedbench_draft.h: this is
// an experimental candidate whose behavior comes from measured winners in
// the experiment ledger; rework is expected and nothing here is final.
#include "embedbench_draft.h"
#include "embedbench_device.h"

#include <Arduino.h>
#include <HostBus.h>
#include <HostClock.h>
#include <HostInterrupt.h>
#include <HostLifecycle.h>
#include <HostUart.h>
#include <SPI.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ebd {
namespace {

constexpr size_t kCapacity = 64;
constexpr size_t kMaxWireDevices = 4;
constexpr size_t kMaxFormats = 8;
constexpr uint32_t kMaxFrameBits = 64;
constexpr size_t kDeferralCapacity = 4;
constexpr size_t kMaxListeners = 4;

struct ListenerSlot {
  EventListener fn = nullptr;
  void* user = nullptr;
};

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
  uint8_t bus = 0;  // which TwoWire instance this endpoint sits on
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
  UartTxHandler uart2Handler = nullptr;
  void* uart2User = nullptr;
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

  // Repeated start is per bus: for each TwoWire instance, the address
  // whose last transfer ended without STOP, or 0xFFFF. A transfer to
  // another address on that bus closes the sequence; the other bus is
  // unaffected.
  uint16_t i2cOpenAddress[2] = {0xFFFF, 0xFFFF};

  TickDeviceFn tickDevice = nullptr;
  void* tickDeviceUser = nullptr;

  // Event listeners: observers of the recorded stream, never responders.
  ListenerSlot listeners[kMaxListeners];
  uint32_t rejectedListeners = 0;
  bool dispatching = false;

  // A device's outstanding wake request, or 0 when there is none.
  uint64_t wakeAtUs = 0;

  // Lifecycle-driven run window.
  bool windowArmed = false;
  bool windowClosed = false;
  uint32_t windowLoops = 0;
  uint32_t windowTickUs = 1000;
  uint32_t loopsDone = 0;
};

// A function-local static, not a namespace-scope object: a sketch's own
// global constructor (arming the run window, for example) can run before
// this translation unit's statics would have been constructed, and would
// then write into an object that is about to be default-initialized. The
// host core keeps its interrupt table this way for the same reason.
State& st() {
  static State instance;
  return instance;
}

uint8_t currentCtx() {
  if (st().isrDepth > 0) return 2;
  if (st().dirDepth > 0) return 1;
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
  const uint32_t seq = st().nextSeq++;
  if (!st().running || st().count >= kCapacity) {
    if (st().running) ++st().dropped;
    return seq;
  }
  Event& e = st().buf[st().count++];
  e.seq = seq;
  e.timeUs = st().vnow;
  e.ctx = currentCtx();
  e.origin = origin;
  e.link = link;
  vsnprintf(e.text, sizeof(e.text), fmt, ap);

  // Fan out to observers. Index iteration over stable slots keeps this
  // safe when a listener removes itself or another one mid-event (X9);
  // the guard stops a listener that records from recursing.
  if (!st().dispatching) {
    st().dispatching = true;
    const Event snapshot = e;
    for (size_t i = 0; i < kMaxListeners; ++i) {
      if (st().listeners[i].fn != nullptr) {
        st().listeners[i].fn(snapshot, st().listeners[i].user);
      }
    }
    st().dispatching = false;
  }
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

// CRC-8/ATM. A byte sum is blind to reordering by construction, and bulk
// payloads are full of transpositions, so summaries carry a CRC instead
// (measured in tests/bulk_checksum).
// One byte folded into a running CRC, for summaries built incrementally.
uint8_t crc8Step(uint8_t crc, uint8_t value) {
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                       : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}

uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

// Payloads up to four bytes print as hex; larger ones are summarized as
// length plus CRC so bulk frames stay one readable line (SCOPE 3.2).
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
    snprintf(out, cap, "len=%u crc=%02X", static_cast<unsigned>(bytes),
             crc8(data, bytes));
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

WireDeviceSlot* findWireDevice(uint8_t bus, uint16_t address) {
  for (size_t i = 0; i < kMaxWireDevices; ++i) {
    if (st().wireDevices[i].used && st().wireDevices[i].bus == bus &&
        st().wireDevices[i].address == address) {
      return &st().wireDevices[i];
    }
  }
  return nullptr;
}

// --- Device call bracketing (re-entrancy contract) ------------------------

void enterDevice() {
  ++st().deviceDepth;
  if (st().deviceDepth > st().maxDeviceDepth) {
    st().maxDeviceDepth = st().deviceDepth;
  }
}

void leaveDevice() {
  if (st().deviceDepth > 0) --st().deviceDepth;
}

// Hold an effect raised inside a device call. Beyond the capacity the
// effect is diagnosed and dropped: never delivered re-entrantly, never
// lost in silence.
void queueDeferred(const Deferred& effect) {
  if (st().deferredCount < kDeferralCapacity) {
    st().deferred[st().deferredCount++] = effect;
    if (effect.kind == 0) ++st().deferredIsrs;
    else ++st().deferredFrames;
    return;
  }
  ++st().deferredDropped;
  ++st().diagCount;
  if (effect.kind == 0) {
    recordf(Origin::kDiag, 0, "diag.deferred_full kind=isr pin=%u", effect.pin);
  } else {
    recordf(Origin::kDiag, 0, "diag.deferred_full kind=frame bus=%u", effect.bus);
  }
}

// Called once the operation that ran a device is fully recorded: deliver
// the held effects in order, each as a fresh top-level call chain.
void deliverDeferred() {
  while (st().deviceDepth == 0 && st().deferredCount > 0) {
    const Deferred effect = st().deferred[0];
    for (size_t i = 1; i < st().deferredCount; ++i) {
      st().deferred[i - 1] = st().deferred[i];
    }
    --st().deferredCount;
    if (effect.kind == 0) {
      HostArduino::triggerInterrupt(effect.pin);
    } else if (st().frameReceiver) {
      st().frameReceiver(effect.bus, effect.format, effect.data, effect.bits,
                          st().frameReceiverUser);
    }
  }
}

// --- Clock hooks --------------------------------------------------------

uint64_t onNow(void*) { return st().vnow; }

void fireTick() {
  ++st().ticks;
  ++st().dirDepth;
  if (st().tickDevice) {
    enterDevice();
    st().tickDevice(st().vnow, st().tickDeviceUser);
    leaveDevice();
    deliverDeferred();
  }
  if (st().tickHandler) st().tickHandler(st().ticks, st().tickUser);
  --st().dirDepth;
}

void onWait(uint32_t us, void*) {
  if (us == 0) {
    // The only external-processing opportunity of a busy-waiting sketch
    // (X7); never re-entered from director or ISR context.
    if (st().dirDepth > 0 || st().isrDepth > 0) {
      ++st().zeroInDirector;
      return;
    }
    ++st().zeroWaits;
    if (st().zeroHandler) {
      ++st().dirDepth;
      st().zeroHandler(st().zeroWaits, st().zeroUser);
      --st().dirDepth;
    }
    return;
  }
  if (st().dirDepth > 0 || st().isrDepth > 0) {
    // Nested wait inside a director or ISR: advance time, defer the tick
    // firing to the outer splitter (X8's only clean policy).
    const uint64_t target = st().vnow + us;
    while (st().nextTick <= target) {
      ++st().pendingTicks;
      st().nextTick += st().tickUs;
    }
    st().vnow = target;
    return;
  }
  const uint64_t target = st().vnow + us;
  for (;;) {
    // The next stop is a tick boundary, or a device's requested wake when
    // that falls earlier: a latency that does not divide by the tick is
    // still served at the moment it is due.
    uint64_t next = st().nextTick;
    bool isWake = false;
    if (st().wakeAtUs != 0 && st().wakeAtUs > st().vnow &&
        st().wakeAtUs < next) {
      next = st().wakeAtUs;
      isWake = true;
    }
    if (next > target) break;
    st().vnow = next;
    if (isWake) {
      st().wakeAtUs = 0;
      ++st().dirDepth;
      if (st().tickDevice) {
        enterDevice();
        st().tickDevice(st().vnow, st().tickDeviceUser);
        leaveDevice();
        deliverDeferred();
      }
      --st().dirDepth;
      // Return with the slice unfinished. The core's own wait loops run
      // until their deadline, so they call back in; stopping here is what
      // lets the application see the device's answer at the moment it was
      // produced instead of at the end of the slice it fell in.
      return;
    }
    st().nextTick += st().tickUs;
    if (st().wakeAtUs != 0 && st().wakeAtUs <= st().vnow) st().wakeAtUs = 0;
    fireTick();
    while (st().pendingTicks > 0) {
      --st().pendingTicks;
      ++st().lateTicks;
      fireTick();
    }
  }
  if (target > st().vnow) st().vnow = target;
}

// --- GPIO and interrupt hooks --------------------------------------------

void onPinWrite(uint8_t pin, uint8_t value, void*) {
  recordf(Origin::kApp, 0, "gpio.write pin=%u val=%u", pin, value);
  if (st().pinForward) {
    // The forward routes into a device's lineIn: a device call.
    enterDevice();
    st().pinForward(pin, value, st().pinForwardUser);
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
      ++st().isrDepth;
      recordf(Origin::kCore, 0, "isr.enter pin=%u", slot.pin);
      break;
    case HostArduino::kInterruptExit:
      recordf(Origin::kCore, 0, "isr.exit pin=%u", slot.pin);
      if (st().isrDepth > 0) --st().isrDepth;
      break;
  }
}

// --- Wire hooks: request/response two-line events (X20 winner) ----------

uint8_t onWireWriteOn(uint8_t bus, uint8_t address, const uint8_t* data,
                      size_t len, bool stop) {
  char hex[12];
  char busTag[8] = {0};
  if (bus != 0) snprintf(busTag, sizeof(busTag), " bus=%u", bus);
  hexOf(data, len, hex, sizeof(hex));
  WireDeviceSlot* dev = findWireDevice(bus, address);
  const bool continued = dev != nullptr && st().i2cOpenAddress[bus] == address;
  const uint32_t req = recordf(Origin::kApp, 0,
                               "i2c.req addr=%02X%s data=%s stop=%u%s", address,
                               busTag, hex, stop ? 1 : 0,
                               continued ? " rs" : "");
  uint8_t status = 2;  // address NACK when no responder is bound (X11)
  if (dev != nullptr) {
    enterDevice();
    status = dev->ops.onWrite(data, len, stop, continued, dev->ops.user);
    leaveDevice();
    if (status > ebdev::kI2cOther) {
      ++st().diagCount;
      recordf(Origin::kDiag, req, "diag.i2c_status addr=%02X status=%u", address,
              status);
    }
  } else {
    ++st().diagCount;
    recordf(Origin::kDiag, req, "diag.unbound addr=%02X", address);
  }
  st().i2cOpenAddress[bus] = stop ? 0xFFFF : address;
  recordf(Origin::kDev, req, "i2c.resp status=%u", status);
  deliverDeferred();
  return status;
}

uint8_t onWireWrite(uint8_t address, const uint8_t* data, size_t len,
                    bool stop, void*) {
  return onWireWriteOn(0, address, data, len, stop);
}

uint8_t onWire1Write(uint8_t address, const uint8_t* data, size_t len,
                     bool stop, void*) {
  return onWireWriteOn(1, address, data, len, stop);
}

size_t onWireReadOn(uint8_t bus, uint8_t address, uint8_t* data, size_t len,
                    bool stop) {
  char busTag[8] = {0};
  if (bus != 0) snprintf(busTag, sizeof(busTag), " bus=%u", bus);
  WireDeviceSlot* dev = findWireDevice(bus, address);
  const bool continued = dev != nullptr && st().i2cOpenAddress[bus] == address;
  const uint32_t req = recordf(Origin::kApp, 0,
                               "i2c.rd.req addr=%02X%s req=%u stop=%u%s",
                               address, busTag, static_cast<unsigned>(len),
                               stop ? 1 : 0, continued ? " rs" : "");
  size_t count = 0;
  if (dev != nullptr) {
    enterDevice();
    count = dev->ops.onRead(data, len, stop, continued, dev->ops.user);
    leaveDevice();
    if (count > len) {
      // A model that claims more bytes than the buffer holds would make
      // the master read past it: diagnose and treat as nothing supplied.
      ++st().diagCount;
      recordf(Origin::kDiag, req, "diag.i2c_read_length addr=%02X got=%u max=%u",
              address, static_cast<unsigned>(count), static_cast<unsigned>(len));
      count = 0;
    }
  } else {
    ++st().diagCount;
    recordf(Origin::kDiag, req, "diag.unbound addr=%02X", address);
  }
  st().i2cOpenAddress[bus] = stop ? 0xFFFF : address;
  char hex[12];
  hexOf(data, count, hex, sizeof(hex));
  recordf(Origin::kDev, req, "i2c.rd.resp len=%u data=%s",
          static_cast<unsigned>(count), hex);
  deliverDeferred();
  return count;
}

size_t onWireRead(uint8_t address, uint8_t* data, size_t len, bool stop,
                  void*) {
  return onWireReadOn(0, address, data, len, stop);
}

size_t onWire1Read(uint8_t address, uint8_t* data, size_t len, bool stop,
                   void*) {
  return onWireReadOn(1, address, data, len, stop);
}

// --- SPI hooks: request/response pair per byte outside a transaction; a
// transaction coalesces its bytes into one summary event (count plus
// checksums), which is the bulk-recording candidate from SCOPE 3.2.

uint8_t onSpiTransfer(uint8_t mosi, void*) {
  if (st().inSpiTransaction) {
    uint8_t miso = 0xFF;
    if (st().spiHandler != nullptr) {
      enterDevice();
      miso = st().spiHandler(mosi, st().spiUser);
      leaveDevice();
    }
    ++st().spiBulkCount;
    st().spiMosiSum = crc8Step(st().spiMosiSum, mosi);
    st().spiMisoSum = crc8Step(st().spiMisoSum, miso);
    return miso;
  }
  const uint32_t req = recordf(Origin::kApp, 0, "spi.req mosi=%02X", mosi);
  uint8_t miso = 0xFF;  // host default: idle bus
  if (st().spiHandler != nullptr) {
    enterDevice();
    miso = st().spiHandler(mosi, st().spiUser);
    leaveDevice();
  } else {
    ++st().diagCount;
    recordf(Origin::kDiag, req, "diag.unbound spi");
  }
  recordf(Origin::kDev, req, "spi.resp miso=%02X", miso);
  deliverDeferred();
  return miso;
}

void onSpiTransaction(bool begin, const SPISettings&, void*) {
  if (begin) {
    st().inSpiTransaction = true;
    st().spiBulkCount = 0;
    st().spiMosiSum = 0;
    st().spiMisoSum = 0;
    recordf(Origin::kApp, 0, "spi.begin");
  } else {
    st().inSpiTransaction = false;
    recordf(Origin::kApp, 0, "spi.bulk n=%u mosi_crc=%02X miso_crc=%02X",
            st().spiBulkCount, st().spiMosiSum, st().spiMisoSum);
    deliverDeferred();
  }
}

// --- Lifecycle hook: the run window opens at preSetup and closes at the
// postLoop of the configured loop, so the core owns the window and the
// application never takes part.

void openWindow(uint32_t tickUs);   // defined with the public API below
void closeWindow();

void onLifecycle(HostArduino::LifecyclePhase phase, void*) {
  switch (phase) {
    case HostArduino::kPreSetup:
      if (st().windowArmed && !st().windowClosed) {
        openWindow(st().windowTickUs);
        recordf(Origin::kCore, 0, "life.pre_setup");
      }
      break;
    case HostArduino::kPostSetup:
      recordf(Origin::kCore, 0, "life.post_setup");
      break;
    case HostArduino::kPreLoop:
      recordf(Origin::kCore, 0, "life.pre_loop n=%u", st().loopsDone + 1);
      break;
    case HostArduino::kPostLoop:
      ++st().loopsDone;
      recordf(Origin::kCore, 0, "life.post_loop n=%u", st().loopsDone);
      if (st().windowArmed && !st().windowClosed &&
          st().loopsDone >= st().windowLoops) {
        recordf(Origin::kCore, 0, "life.window_end loops=%u", st().loopsDone);
        st().windowClosed = true;
        closeWindow();
      }
      break;
    default:
      break;
  }
}

// --- Analog hooks: reads are request/response pairs like the buses;
// writes and configuration are single events. Analog inputs are held
// environment state injected by the director, so no device answers here.

uint16_t onAnalogRead(uint8_t pin, uint16_t held, void*) {
  const uint32_t req = recordf(Origin::kApp, 0, "analog.req pin=%u", pin);
  recordf(Origin::kCore, req, "analog.resp val=%u", held);
  return held;
}

uint32_t onAnalogMilliVolts(uint8_t pin, uint32_t held, void*) {
  const uint32_t req = recordf(Origin::kApp, 0, "analog.mv.req pin=%u", pin);
  recordf(Origin::kCore, req, "analog.mv.resp mv=%u",
          static_cast<unsigned>(held));
  return held;
}

void onAnalogReadConfig(uint8_t bits, void*) {
  recordf(Origin::kApp, 0, "analog.config bits=%u", bits);
}

const char* analogEventName(HostArduino::AnalogWriteEvent event) {
  switch (event) {
    case HostArduino::kAnalogAttach:
      return "attach";
    case HostArduino::kAnalogWrite:
      return "write";
    case HostArduino::kAnalogConfig:
      return "config";
    case HostArduino::kAnalogTone:
      return "tone";
    case HostArduino::kAnalogDetach:
      return "detach";
    default:
      return "dac";
  }
}

void onAnalogWrite(HostArduino::AnalogWriteEvent event,
                   const HostArduino::AnalogOut& out, void*) {
  recordf(Origin::kApp, 0, "analog.out %s pin=%u duty=%u hz=%u",
          analogEventName(event), out.pin, static_cast<unsigned>(out.duty),
          static_cast<unsigned>(out.frequency));
}

// --- UART hook: device replies go through the RX sink (X21) --------------

void onUartActivity(HostUart::ActivityEvent event, HostUart& uart,
                    const uint8_t* data, size_t len, void*) {
  // Two device-facing ports exist; the instance says which one spoke, and
  // only the second is tagged so single-port traces stay as they were.
  const bool second = uart.uartNum() == 2;
  char portTag[8] = {0};
  if (second) snprintf(portTag, sizeof(portTag), " port=2");
  UartTxHandler handler = second ? st().uart2Handler : st().uartHandler;
  void* handlerUser = second ? st().uart2User : st().uartUser;
  switch (event) {
    case HostUart::kUartTx: {
      char text[24];
      bytesLabel(data, len, text, sizeof(text));
      recordf(Origin::kApp, 0, "uart.tx%s %s", portTag, text);
      if (handler) {
        enterDevice();
        handler(data, len, handlerUser);
        leaveDevice();
        deliverDeferred();
      }
      break;
    }
    case HostUart::kUartRx:
      if (data[0] >= 0x20 && data[0] <= 0x7E) {
        recordf(Origin::kApp, 0, "uart.rx%s %c", portTag, data[0]);
      } else {
        recordf(Origin::kApp, 0, "uart.rx%s 0x%02X", portTag, data[0]);
      }
      break;
    case HostUart::kUartBegin:
      recordf(Origin::kApp, 0, "uart.begin%s", portTag);
      break;
    case HostUart::kUartEnd:
      recordf(Origin::kApp, 0, "uart.end%s", portTag);
      break;
    case HostUart::kUartConfig:
      recordf(Origin::kApp, 0, "uart.config%s", portTag);
      break;
    case HostUart::kUartRxDiscard:
      recordf(Origin::kApp, 0, "uart.rx_discard%s len=%u", portTag,
              static_cast<unsigned>(len));
      break;
  }
}

}  // namespace

// --- Public draft API -----------------------------------------------------

bool bindWireDeviceOn(WireBus bus, uint16_t address,
                      const WireDeviceOps& ops) {
  const uint8_t busNum = static_cast<uint8_t>(bus);
  if (findWireDevice(busNum, address) != nullptr) {
    ++st().diagCount;
    return false;
  }
  for (size_t i = 0; i < kMaxWireDevices; ++i) {
    if (!st().wireDevices[i].used) {
      st().wireDevices[i].used = true;
      st().wireDevices[i].bus = busNum;
      st().wireDevices[i].address = address;
      st().wireDevices[i].ops = ops;
      return true;
    }
  }
  ++st().diagCount;
  return false;
}

bool bindWireDevice(uint16_t address, const WireDeviceOps& ops) {
  return bindWireDeviceOn(WireBus::kWire0, address, ops);
}

void bindUartDeviceOn(SerialPort port, UartTxHandler handler, void* user) {
  if (port == SerialPort::kSerial2) {
    st().uart2Handler = handler;
    st().uart2User = user;
  } else {
    st().uartHandler = handler;
    st().uartUser = user;
  }
}

void bindUartDevice(UartTxHandler handler, void* user) {
  bindUartDeviceOn(SerialPort::kSerial1, handler, user);
}

void setChannelHandler(ChannelHandler handler, void* user) {
  st().channelHandler = handler;
  st().channelUser = user;
}

void bindSpiDevice(SpiTransferFn handler, void* user) {
  st().spiHandler = handler;
  st().spiUser = user;
}

void setPinWriteForward(PinWriteForward handler, void* user) {
  st().pinForward = handler;
  st().pinForwardUser = user;
}

void bindFrameDevice(FrameHandler handler, void* user) {
  st().frameDevice = handler;
  st().frameDeviceUser = user;
}

bool addListener(EventListener fn, void* user) {
  if (fn == nullptr) return false;
  for (size_t i = 0; i < kMaxListeners; ++i) {
    if (st().listeners[i].fn == nullptr) {
      st().listeners[i].fn = fn;
      st().listeners[i].user = user;
      return true;
    }
  }
  ++st().rejectedListeners;
  ++st().diagCount;
  recordf(Origin::kDiag, 0, "diag.listener_full cap=%u",
          static_cast<unsigned>(kMaxListeners));
  return false;
}

bool removeListener(EventListener fn) {
  for (size_t i = 0; i < kMaxListeners; ++i) {
    if (st().listeners[i].fn == fn) {
      st().listeners[i].fn = nullptr;
      st().listeners[i].user = nullptr;
      return true;
    }
  }
  return false;
}

size_t listenerCapacity() { return kMaxListeners; }

void setFrameReceiver(FrameHandler handler, void* user) {
  st().frameReceiver = handler;
  st().frameReceiverUser = user;
}

void setTickHandler(TickHandler handler, void* user) {
  st().tickHandler = handler;
  st().tickUser = user;
}

void bindTickDevice(TickDeviceFn fn, void* user) {
  st().tickDevice = fn;
  st().tickDeviceUser = user;
}

void setZeroWaitHandler(ZeroWaitHandler handler, void* user) {
  st().zeroHandler = handler;
  st().zeroUser = user;
}

void runBegin(uint32_t tickUs) {
  st().count = 0;
  st().nextSeq = 1;
  st().dropped = 0;
  st().diagCount = 0;
  st().tickUs = tickUs;
  st().vnow = 0;
  st().nextTick = tickUs;
  st().ticks = 0;
  st().pendingTicks = 0;
  st().lateTicks = 0;
  st().zeroWaits = 0;
  st().zeroInDirector = 0;
  st().dirDepth = 0;
  st().isrDepth = 0;
  st().inSpiTransaction = false;
  st().spiBulkCount = 0;
  st().spiMosiSum = 0;
  st().spiMisoSum = 0;
  st().deviceDepth = 0;
  st().maxDeviceDepth = 0;
  st().deferredCount = 0;
  st().deferredIsrs = 0;
  st().deferredFrames = 0;
  st().deferredDropped = 0;
  st().i2cOpenAddress[0] = 0xFFFF;
  st().i2cOpenAddress[1] = 0xFFFF;
  st().wakeAtUs = 0;
  st().running = true;

  HostArduino::setPinWriteHook(&onPinWrite);
  HostArduino::setPinReadHook(&onPinRead);
  HostArduino::setPinModeHook(&onPinMode);
  HostArduino::setInterruptHook(&onInterruptEvent);
  Wire.setWriteHook(&onWireWrite);
  Wire.setReadHook(&onWireRead);
  Wire1.setWriteHook(&onWire1Write);
  Wire1.setReadHook(&onWire1Read);
  HostArduino::setAnalogReadHook(&onAnalogRead);
  HostArduino::setAnalogMilliVoltsHook(&onAnalogMilliVolts);
  HostArduino::setAnalogReadConfigHook(&onAnalogReadConfig);
  HostArduino::setAnalogWriteHook(&onAnalogWrite);
  SPI.setTransferHook(&onSpiTransfer);
  SPI.setTransactionHook(&onSpiTransaction);
  Serial1.setActivityHook(&onUartActivity);
  Serial2.setActivityHook(&onUartActivity);
  HostArduino::setClockHooks(&onNow, &onWait);
}

void runEnd() {
  st().running = false;
  HostArduino::clearClockHooks();
  Serial1.clearActivityHook();
  Serial2.clearActivityHook();
  Wire.clearHooks();
  Wire1.clearHooks();
  SPI.clearHooks();
  HostArduino::clearInterruptHook();
  HostArduino::clearAnalogHooks();
  HostArduino::clearPinHooks();
}

namespace {

// The lifecycle hook opens and closes the window through the same entry
// points a test would call by hand, so both paths behave identically.
void openWindow(uint32_t tickUs) { runBegin(tickUs); }
void closeWindow() { runEnd(); }

}  // namespace

void armRunWindow(uint32_t tickUs, uint32_t loops) {
  st().windowArmed = true;
  st().windowClosed = false;
  st().windowLoops = loops > 0 ? loops : 1;
  st().windowTickUs = tickUs > 0 ? tickUs : 1;
  st().loopsDone = 0;
  // Registering here means "before main" when called from a global
  // constructor, which is the only moment kPreSetup can still be caught.
  HostArduino::setLifecycleHook(&onLifecycle);
}

bool runWindowClosed() { return st().windowClosed; }

uint32_t completedLoops() { return st().loopsDone; }

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
  if (st().deviceDepth > 0) {
    // A device raised this line from inside one of its methods: the ISR
    // runs after that device call has completed (re-entrancy contract).
    Deferred effect = {0, pin, 0, 0, 0, {0}};
    queueDeferred(effect);
    return;
  }
  HostArduino::triggerInterrupt(pin);
}

bool uartInjectOn(Origin origin, SerialPort port, const uint8_t* data,
                  size_t len) {
  const bool second = port == SerialPort::kSerial2;
  char text[24];
  char portTag[8] = {0};
  if (second) snprintf(portTag, sizeof(portTag), " port=2");
  bytesLabel(data, len, text, sizeof(text));
  recordf(origin, 0, "dev.tx%s %s", portTag, text);
  const size_t accepted =
      second ? Serial2.pushRx(data, len) : Serial1.pushRx(data, len);
  if (accepted < len) {
    ++st().diagCount;
    recordf(Origin::kDiag, 0, "diag.uart_rx_full accepted=%u len=%u",
            static_cast<unsigned>(accepted), static_cast<unsigned>(len));
    return false;
  }
  return true;
}

bool uartInject(Origin origin, const uint8_t* data, size_t len) {
  return uartInjectOn(origin, SerialPort::kSerial1, data, len);
}

void chanWrite(Origin origin, uint8_t channel, const uint8_t* data,
               size_t len) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  recordf(origin, 0, "chan.write chan=%u data=%s", channel, hex);
  if (st().channelHandler) {
    enterDevice();
    const bool applied =
        st().channelHandler(channel, data, len, st().channelUser);
    leaveDevice();
    if (!applied) {
      ++st().diagCount;
      recordf(Origin::kDiag, 0, "diag.chan_reject chan=%u len=%u", channel,
              static_cast<unsigned>(len));
    }
    deliverDeferred();
  }
}

// Format labels: a registered id prints its name, an unregistered id
// prints its number, so raw-numbered experiments keep working.
void formatLabel(uint16_t id, char* out, size_t cap) {
  if (id >= 1 && id <= kMaxFormats && st().formats[id - 1].used) {
    snprintf(out, cap, "%s", st().formats[id - 1].name);
  } else {
    snprintf(out, cap, "%u", id);
  }
}

// Atomic acceptance: every refusal is a diagnostic, never a truncation.
bool frameAccept(uint8_t bus, uint16_t format, const uint8_t* data,
                 size_t bits) {
  if (format == 0) {
    ++st().diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_noformat bus=%u", bus);
    return false;
  }
  if (format > kMaxFormats || !st().formats[format - 1].used) {
    // Only ids handed out by registerFormat() are valid: a raw number
    // would bypass the name + schema collision check.
    ++st().diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_unknown_format bus=%u fmt=%u", bus,
            format);
    return false;
  }
  if (bits > kMaxFrameBits) {
    ++st().diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_oversize bus=%u bits=%u max=%u", bus,
            static_cast<unsigned>(bits), kMaxFrameBits);
    return false;
  }
  if (bits > 0 && data == nullptr) {
    ++st().diagCount;
    recordf(Origin::kDiag, 0, "diag.frame_nodata bus=%u bits=%u", bus,
            static_cast<unsigned>(bits));
    return false;
  }
  if (bits > 0 && !ebdev::framePaddingClean(data, bits)) {
    ++st().diagCount;
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
  if (st().frameDevice) {
    enterDevice();
    st().frameDevice(bus, format, data, bits, st().frameDeviceUser);
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
  if (st().frameReceiver) {
    if (st().deviceDepth > 0) {
      // Raised from inside a device method: the application receiver
      // runs after that device call has completed (re-entrancy contract).
      Deferred effect = {1, 0, bus, format, static_cast<uint8_t>(bits), {0}};
      const size_t bytes = ebdev::frameBytes(bits);
      for (size_t i = 0; i < bytes && i < sizeof(effect.data); ++i) {
        effect.data[i] = data[i];
      }
      queueDeferred(effect);
    } else {
      st().frameReceiver(bus, format, data, bits, st().frameReceiverUser);
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
    ++st().diagCount;
    recordf(Origin::kDiag, 0, "diag.fmt_name_long len=%u",
            static_cast<unsigned>(length));
    return 0;
  }
  for (size_t i = 0; i < kMaxFormats; ++i) {
    if (st().formats[i].used && strcmp(st().formats[i].name, name) == 0) {
      if (st().formats[i].schema != schema) {
        // Same name, different layout: two libraries collided on a name.
        ++st().diagCount;
        recordf(Origin::kDiag, 0, "diag.fmt_conflict name=%s", name);
        return 0;
      }
      return static_cast<uint16_t>(i + 1);
    }
  }
  for (size_t i = 0; i < kMaxFormats; ++i) {
    if (!st().formats[i].used) {
      st().formats[i].used = true;
      st().formats[i].schema = schema;
      snprintf(st().formats[i].name, sizeof(st().formats[i].name), "%s",
               name);
      return static_cast<uint16_t>(i + 1);
    }
  }
  ++st().diagCount;
  recordf(Origin::kDiag, 0, "diag.fmt_full name=%s", name);
  return 0;
}

void analogInject(Origin origin, uint8_t pin, uint16_t raw) {
  recordf(origin, 0, "analog.inject pin=%u val=%u", pin, raw);
  HostArduino::setAnalogValue(pin, raw);
}

void analogInjectMilliVolts(Origin origin, uint8_t pin, uint32_t mv) {
  recordf(origin, 0, "analog.inject.mv pin=%u mv=%u", pin,
          static_cast<unsigned>(mv));
  HostArduino::setAnalogMilliVolts(pin, mv);
}

void deviceNote(const char* text) {
  recordf(Origin::kDev, 0, "dev.note %s", text);
}

bool requestWake(uint64_t whenUs) {
  // Keep the earliest outstanding request: an environment may always
  // advance more often than asked, never less.
  if (st().wakeAtUs == 0 || whenUs < st().wakeAtUs) st().wakeAtUs = whenUs;
  return true;
}

uint64_t pendingWakeUs() { return st().wakeAtUs; }

void dumpf(const char* fmt, ...) {
  char text[50];  // fills the 56-byte event text after the "dump " prefix
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  recordf(Origin::kDir, 0, "dump %s", text);
}

uint64_t nowUs() { return st().vnow; }

Stats stats() {
  Stats s;
  s.events = static_cast<uint32_t>(st().count);
  s.dropped = st().dropped;
  s.zeroWaits = st().zeroWaits;
  s.zeroInDirector = st().zeroInDirector;
  s.lateTicks = st().lateTicks;
  s.ticks = st().ticks;
  s.diagCount = st().diagCount;
  s.deferredIsrs = st().deferredIsrs;
  s.deferredFrames = st().deferredFrames;
  s.deferredDropped = st().deferredDropped;
  s.maxDeviceDepth = st().maxDeviceDepth;
  return s;
}

size_t eventCount() { return st().count; }

size_t respLineCount() {
  size_t count = 0;
  for (size_t i = 0; i < st().count; ++i) {
    if (st().buf[i].link != 0) ++count;
  }
  return count;
}

size_t eventBytes() { return sizeof(Event); }

size_t formatTrace(char* out, size_t cap) {
  size_t pos = 0;
  for (size_t i = 0; i < st().count && pos < cap; ++i) {
    const Event& e = st().buf[i];
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
