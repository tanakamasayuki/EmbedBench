// EmbedBench draft core implementation. See embedbench_draft.h: this is
// an experimental candidate whose behavior comes from measured winners in
// the experiment ledger; rework is expected and nothing here is final.
#include "embedbench_draft.h"

#include <Arduino.h>
#include <HostBus.h>
#include <HostClock.h>
#include <HostInterrupt.h>
#include <HostUart.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>

namespace ebd {
namespace {

constexpr size_t kCapacity = 64;
constexpr size_t kMaxWireDevices = 2;

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
  TickHandler tickHandler = nullptr;
  void* tickUser = nullptr;
  ZeroWaitHandler zeroHandler = nullptr;
  void* zeroUser = nullptr;
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

WireDeviceSlot* findWireDevice(uint16_t address) {
  for (size_t i = 0; i < kMaxWireDevices; ++i) {
    if (state.wireDevices[i].used && state.wireDevices[i].address == address) {
      return &state.wireDevices[i];
    }
  }
  return nullptr;
}

// --- Clock hooks --------------------------------------------------------

uint64_t onNow(void*) { return state.vnow; }

void fireTick() {
  ++state.ticks;
  ++state.dirDepth;
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

uint8_t onWireWrite(uint8_t address, const uint8_t* data, size_t len, bool,
                    void*) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  const uint32_t req =
      recordf(Origin::kApp, 0, "i2c.req addr=%02X data=%s", address, hex);
  WireDeviceSlot* dev = findWireDevice(address);
  uint8_t status = 2;  // address NACK when no responder is bound (X11)
  if (dev != nullptr) {
    status = dev->ops.onWrite(data, len, dev->ops.user);
  } else {
    ++state.diagCount;
    recordf(Origin::kDiag, req, "diag.unbound addr=%02X", address);
  }
  recordf(Origin::kDev, req, "i2c.resp status=%u", status);
  return status;
}

size_t onWireRead(uint8_t address, uint8_t* data, size_t len, bool, void*) {
  const uint32_t req = recordf(Origin::kApp, 0, "i2c.rd.req addr=%02X req=%u",
                               address, static_cast<unsigned>(len));
  WireDeviceSlot* dev = findWireDevice(address);
  size_t count = 0;
  if (dev != nullptr) {
    count = dev->ops.onRead(data, len, dev->ops.user);
  } else {
    ++state.diagCount;
    recordf(Origin::kDiag, req, "diag.unbound addr=%02X", address);
  }
  char hex[12];
  hexOf(data, count, hex, sizeof(hex));
  recordf(Origin::kDev, req, "i2c.rd.resp len=%u data=%s",
          static_cast<unsigned>(count), hex);
  return count;
}

// --- UART hook: device replies go through the RX sink (X21) --------------

void onUartActivity(HostUart::ActivityEvent event, HostUart&,
                    const uint8_t* data, size_t len, void*) {
  switch (event) {
    case HostUart::kUartTx: {
      char text[24];
      snprintf(text, sizeof(text), "%.*s", static_cast<int>(len),
               reinterpret_cast<const char*>(data));
      recordf(Origin::kApp, 0, "uart.tx %s", text);
      if (state.uartHandler) state.uartHandler(data, len, state.uartUser);
      break;
    }
    case HostUart::kUartRx:
      recordf(Origin::kApp, 0, "uart.rx %c", data[0]);
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

void setTickHandler(TickHandler handler, void* user) {
  state.tickHandler = handler;
  state.tickUser = user;
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
  state.running = true;

  HostArduino::setPinWriteHook(&onPinWrite);
  HostArduino::setPinReadHook(&onPinRead);
  HostArduino::setPinModeHook(&onPinMode);
  HostArduino::setInterruptHook(&onInterruptEvent);
  Wire.setWriteHook(&onWireWrite);
  Wire.setReadHook(&onWireRead);
  Serial1.setActivityHook(&onUartActivity);
  HostArduino::setClockHooks(&onNow, &onWait);
}

void runEnd() {
  state.running = false;
  HostArduino::clearClockHooks();
  Serial1.clearActivityHook();
  Wire.clearHooks();
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
  if (match) HostArduino::triggerInterrupt(pin);
}

void uartInject(Origin origin, const char* bytes) {
  recordf(origin, 0, "dev.tx %s", bytes);
  Serial1.pushRx(bytes);
}

void chanWrite(Origin origin, uint8_t channel, const uint8_t* data,
               size_t len) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  recordf(origin, 0, "chan.write chan=%u data=%s", channel, hex);
  if (state.channelHandler) {
    state.channelHandler(channel, data, len, state.channelUser);
  }
}

void dumpf(const char* fmt, ...) {
  char text[36];
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
