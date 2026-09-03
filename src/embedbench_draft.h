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
  char text[56] = {0};
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
using SpiTransferFn = uint8_t (*)(uint8_t mosi, void* user);
using PinWriteForward = void (*)(uint8_t pin, uint8_t value, void* user);
using FrameHandler = void (*)(uint8_t bus, uint16_t format,
                              const uint8_t* data, size_t bits, void* user);

// Bindings persist across runs; runBegin/runEnd own the host hooks and
// the trace for one run window.
bool bindWireDevice(uint16_t address, const WireDeviceOps& ops);
void bindUartDevice(UartTxHandler handler, void* user = nullptr);
void bindSpiDevice(SpiTransferFn handler, void* user = nullptr);
void setChannelHandler(ChannelHandler handler, void* user = nullptr);
void setTickHandler(TickHandler handler, void* user = nullptr);
void setZeroWaitHandler(ZeroWaitHandler handler, void* user = nullptr);
// Forwards recorded application pin writes (chip-select, data/command
// lines) to whoever routes them into a device model's lineIn.
void setPinWriteForward(PinWriteForward handler, void* user = nullptr);
// Frame routing: the extension path for protocols without a dedicated
// port. bindFrameDevice receives application frames (device's frameIn);
// setFrameReceiver is the application-side shim that receives device
// frames (HostPort::frameOut arrivals).
void bindFrameDevice(FrameHandler handler, void* user = nullptr);
void setFrameReceiver(FrameHandler handler, void* user = nullptr);

void runBegin(uint32_t tickUs);
void runEnd();

// Sinks: every dir/dev-originated external effect is recorded here first,
// then applied (matrix principle 3).
void pinInject(Origin origin, uint8_t pin, uint8_t level);
void uartInject(Origin origin, const char* bytes);
// Logical frames (format id + pre-encoding bits): frameTx carries an
// application frame to the bound device, frameRx carries a device frame
// to the application-side receiver. Both record first.
void frameTx(Origin origin, uint8_t bus, uint16_t format,
             const uint8_t* data, size_t bits);
void frameRx(Origin origin, uint8_t bus, uint16_t format,
             const uint8_t* data, size_t bits);
// Intern a format name: the same name always returns the same nonzero id
// within this environment; 0 when the registry is full. Names, not
// numbers, are the cross-library identity of a format.
uint16_t registerFormat(const char* name);
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
