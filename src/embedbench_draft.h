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
// Scope of this draft (v2): GPIO, interrupts, Wire (global instance), SPI,
// Serial1, analog, a virtual clock with a fixed tick, a lifecycle-driven
// run window, and event listeners. Wire1/Serial2 and multi-instance
// binding are still out.
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
  // A repeating cycle is folded into its first copy when the buffer would
  // otherwise overflow, so `repeats` is 1 for an ordinary event and
  // lastTimeUs is when the last copy happened (X53).
  uint32_t repeats = 1;
  uint64_t lastTimeUs = 0;
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
  uint32_t deferredIsrs = 0;    // ISRs held back while a device ran
  uint32_t deferredFrames = 0;  // application frame deliveries held back
  uint32_t deferredDropped = 0; // effects beyond the deferral capacity
  uint32_t maxDeviceDepth = 0;  // 1 means no device was ever re-entered
  uint32_t folded = 0;          // events kept as a repeat count, not a slot
};

// I2C device callbacks receive the transaction context the master issued:
// `stop` = a STOP follows, `continued` = issued under a repeated start.
struct WireDeviceOps {
  uint8_t (*onWrite)(const uint8_t* data, size_t len, bool stop,
                     bool continued, void* user);
  size_t (*onRead)(uint8_t* data, size_t len, bool stop, bool continued,
                   void* user);
  void* user;
};

using TickHandler = void (*)(uint32_t tick, void* user);
// Advances device models to the tick time. Bracketed as a device call
// (re-entrancy contract) and run before the director's TickHandler.
using TickDeviceFn = void (*)(uint64_t nowUs, void* user);
using ZeroWaitHandler = void (*)(uint32_t count, void* user);
using UartTxHandler = void (*)(const uint8_t* data, size_t len, void* user);
// Returns whether the device applied the whole payload; a false return is
// recorded as a diagnostic (device contract for channelWrite).
using ChannelHandler = bool (*)(uint8_t channel, const uint8_t* data,
                                size_t len, void* user);
using SpiTransferFn = uint8_t (*)(uint8_t mosi, void* user);
using PinWriteForward = void (*)(uint8_t pin, uint8_t value, void* user);
using FrameHandler = void (*)(uint8_t bus, uint16_t format,
                              const uint8_t* data, size_t bits, void* user);
// Every recorded event is also offered to listeners, in slot order, with
// no return value: an observer can watch the stream but never change it
// (X9's observer/responder split).
using EventListener = void (*)(const Event& event, void* user);

// Which instance of a bus a binding refers to. The host core exposes two
// of each; a device owns one endpoint on one of them (interface rule),
// and composite hardware is composed from several devices.
enum class WireBus : uint8_t { kWire0 = 0, kWire1 = 1 };
enum class SerialPort : uint8_t { kSerial1 = 1, kSerial2 = 2 };

// Bindings persist across runs; runBegin/runEnd own the host hooks and
// the trace for one run window.
bool bindWireDevice(uint16_t address, const WireDeviceOps& ops);
bool bindWireDeviceOn(WireBus bus, uint16_t address,
                      const WireDeviceOps& ops);
void bindUartDevice(UartTxHandler handler, void* user = nullptr);
void bindUartDeviceOn(SerialPort port, UartTxHandler handler,
                      void* user = nullptr);
void bindSpiDevice(SpiTransferFn handler, void* user = nullptr);
void setChannelHandler(ChannelHandler handler, void* user = nullptr);
void setTickHandler(TickHandler handler, void* user = nullptr);
void bindTickDevice(TickDeviceFn fn, void* user = nullptr);
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
// Observers of the event stream. addListener returns false when the table
// is full (a diagnostic is recorded); removeListener works from inside a
// callback and takes effect for the events that follow.
bool addListener(EventListener fn, void* user = nullptr);
bool removeListener(EventListener fn);
size_t listenerCapacity();

void runBegin(uint32_t tickUs);
void runEnd();

// Lifecycle-driven run window: arm it from a global constructor (before
// main, the only point where the host core's single lifecycle hook still
// catches kPreSetup) and the window opens at preSetup and closes at the
// postLoop of the `loops`-th completed loop. The core decides when the
// run ends; the application never participates.
void armRunWindow(uint32_t tickUs, uint32_t loops);
bool runWindowClosed();
uint32_t completedLoops();

// Sinks: every dir/dev-originated external effect is recorded here first,
// then applied (matrix principle 3).
void pinInject(Origin origin, uint8_t pin, uint8_t level);
// Binary-safe: any byte value is queued and logged (text when printable,
// otherwise hex / length+checksum). Returns whether every byte was
// accepted; a full receive queue is diagnosed and the remainder dropped.
bool uartInject(Origin origin, const uint8_t* data, size_t len);
bool uartInjectOn(Origin origin, SerialPort port, const uint8_t* data,
                  size_t len);
// Logical frames (format id + pre-encoding bits): frameTx carries an
// application frame to the bound device, frameRx carries a device frame
// to the application-side receiver. Both record first.
// Both return whether the frame was accepted; a refusal (no format,
// oversize, dirty padding, missing data) is recorded as a diagnostic and
// the frame is not delivered — atomic delivery or nothing.
bool frameTx(Origin origin, uint8_t bus, uint16_t format,
             const uint8_t* data, size_t bits);
bool frameRx(Origin origin, uint8_t bus, uint16_t format,
             const uint8_t* data, size_t bits);
// Intern a format name with its layout fingerprint: the same name always
// returns the same nonzero id within this environment; a known name with
// a different schema is a conflict (0 plus a diagnostic); 0 also when the
// registry is full. Names, not numbers, are the cross-library identity.
uint16_t registerFormat(const char* name, uint32_t schema);
// This environment's per-call frame capacity in bits (all buses). An
// oversized frameTx/frameRx is rejected whole with a diagnostic event.
uint32_t frameCapacityBits();
// How many effects (interrupts, application frame deliveries) raised
// while a device runs can be held for delivery after it returns; beyond
// this an effect is diagnosed and dropped, never delivered re-entrantly.
size_t deferralCapacity();
void chanWrite(Origin origin, uint8_t channel, const uint8_t* data,
               size_t len);
// Analog injection sinks: recorded, then applied to the host's held
// values. Analog inputs are environment state (the frozen device
// interface has no analog port), so these are director-side.
void analogInject(Origin origin, uint8_t pin, uint16_t raw);
void analogInjectMilliVolts(Origin origin, uint8_t pin, uint32_t mv);
// Device commentary (HostPort::diagnose, interface revision 004):
// recorded in order among the events, never an effect.
void deviceNote(const char* text);
// A device's wake request (HostPort::requestWake, revision 003). The wait
// splitter stops at the requested time as well as at its tick boundaries,
// so a latency that does not divide by the tick is still served when it is
// due. Only the earliest outstanding request is kept.
bool requestWake(uint64_t whenUs);
uint64_t pendingWakeUs();
void dumpf(const char* fmt, ...);

uint64_t nowUs();
Stats stats();
size_t eventCount();
size_t respLineCount();
size_t eventBytes();
size_t formatTrace(char* out, size_t cap);

}  // namespace ebd
