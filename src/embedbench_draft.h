// EmbedBench draft core — EXPERIMENTAL CANDIDATE, NOT an approved API.
//
// Integrates the winning candidates from the experiment ledger (X4/X7/X8
// tick handling, X11 observer/responder split, X16 edge decision and
// ctx=isr tagging, X20 request/response two-line events, X21 sink-recorded
// device replies) into one core so multi-bus scenarios can be measured.
// Gate A-F are deliberately deferred by the project owner: names, fields,
// and behavior here are provisional and rework is expected. Nothing in
// this header is a public contract.
//
// Scope of this draft (v1): GPIO, interrupts, Wire (global instance),
// Serial1, virtual clock with a fixed tick. Analog, SPI, Wire1/Serial2,
// lifecycle-driven run windows, and listener fan-out are intentionally
// left for a later round.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ebd {

enum class Origin : uint8_t { kApp, kDir, kDev, kCore, kDiag };

// One recorded event. Text payload keeps the draft simple; the binary
// record shape was measured separately in X10.
struct Event {
  uint32_t seq = 0;
  uint64_t timeUs = 0;
  uint8_t ctx = 0;  // 0 = main, 1 = tick (director), 2 = isr
  Origin origin = Origin::kApp;
  uint32_t link = 0;  // request seq for response events, 0 otherwise
  char text[44] = {0};
};

struct Stats {
  uint32_t events = 0;
  uint32_t dropped = 0;
  uint32_t zeroWaits = 0;
  uint32_t zeroInDirector = 0;
  uint32_t lateTicks = 0;
  uint32_t ticks = 0;
  uint32_t diagCount = 0;
};

struct WireDeviceOps {
  uint8_t (*onWrite)(const uint8_t* data, size_t len, void* user);
  size_t (*onRead)(uint8_t* data, size_t len, void* user);
  void* user;
};

using TickHandler = void (*)(uint32_t tick, void* user);
using ZeroWaitHandler = void (*)(uint32_t count, void* user);
using UartTxHandler = void (*)(const uint8_t* data, size_t len, void* user);
using ChannelHandler = void (*)(uint8_t channel, const uint8_t* data,
                                size_t len, void* user);

// Bindings persist across runs; runBegin/runEnd own the host hooks and
// the trace for one run window.
bool bindWireDevice(uint16_t address, const WireDeviceOps& ops);
void bindUartDevice(UartTxHandler handler, void* user = nullptr);
void setChannelHandler(ChannelHandler handler, void* user = nullptr);
void setTickHandler(TickHandler handler, void* user = nullptr);
void setZeroWaitHandler(ZeroWaitHandler handler, void* user = nullptr);

void runBegin(uint32_t tickUs);
void runEnd();

// Sinks: every dir/dev-originated external effect is recorded here first,
// then applied (matrix principle 3).
void pinInject(Origin origin, uint8_t pin, uint8_t level);
void uartInject(Origin origin, const char* bytes);
void chanWrite(Origin origin, uint8_t channel, const uint8_t* data,
               size_t len);
void dumpf(const char* fmt, ...);

uint64_t nowUs();
Stats stats();
size_t eventCount();
size_t respLineCount();
size_t eventBytes();
size_t formatTrace(char* out, size_t cap);

}  // namespace ebd
