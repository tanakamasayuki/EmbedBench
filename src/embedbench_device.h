// EmbedBench device interface — the portable boundary between device
// models and any environment that hosts them.
//
// This is the one surface the project intends to fix: a device model
// written against this header must compile and behave identically as pure
// C++11 with no platform at all, on the Arduino host core, or on any
// future environment. Everything above it (how an environment owns host
// hooks, drives a clock, or records events) is treated as a per-platform
// implementation example — see src/embedbench_draft.* for the
// host-arduino-core one.
//
// Hard rules for this header and for every device model:
//   - include nothing beyond <stddef.h> / <stdint.h> (models may also use
//     the C standard library, e.g. <stdio.h> for snprintf)
//   - no Arduino, host-core, or EmbedBench-environment types
//   - a device never touches the world directly: every outward effect
//     goes through HostPort, every inward stimulus arrives as a call
//   - deterministic: identical call sequences produce identical behavior;
//     time only moves when advanceTo() says so
//
// Still provisional while the draft phase runs, but this file is the
// candidate being hardened, not an implementation detail.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ebdev {

// Services an environment provides to a device model. The environment
// decides what recording, pin mapping, or queueing sits behind each call.
class HostPort {
 public:
  virtual ~HostPort() {}

  // The device's notion of now, in microseconds. Must agree with the
  // values later passed to Device::advanceTo().
  virtual uint64_t nowMicros() = 0;

  // Drive one of the device's logical output lines (IRQ, DRDY, ...).
  // Line ids are device-local; the binding maps them to real pins.
  virtual void lineOut(uint8_t line, uint8_t level) = 0;

  // Send bytes toward the application (the device's serial TX).
  virtual void serialOut(const uint8_t* data, size_t len) = 0;

  // Emit a logical protocol frame toward the application side: the bits
  // as they exist before physical encoding, plus a format id naming how
  // to interpret them. This is the extension path for protocols the
  // interface has no dedicated port for (PIO, IR, WS2812, ...): by
  // project policy the physical layer is never reproduced, so frames —
  // not pin-level bit-banging — are how such devices talk. `bits` may be
  // any bit count; `data` holds ceil(bits / 8) bytes. Optional: an
  // environment without frame routing may leave the default no-op.
  // `bus` is the device-local logical link the frame travels on (a PIO
  // state machine, an IR channel, a CAN bus, ...); the binding maps it to
  // whatever the platform actually has, like line ids.
  virtual void frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                        size_t bits) {
    (void)bus;
    (void)format;
    (void)data;
    (void)bits;
  }

  // Resolve a protocol format NAME to this environment's id for it.
  // Names are the collision-free identity — independent libraries cannot
  // coordinate numbers — and the environment interns them: the same name
  // always returns the same nonzero id within an environment, while the
  // numeric value is environment-local. 0 means no frame routing or a
  // full registry; a device holding id 0 must match no frame. Devices
  // resolve once and cache; every frame call then compares integers.
  virtual uint16_t formatId(const char* name) {
    (void)name;
    return 0;
  }
};

// A device model: a deterministic state machine fed by bus operations,
// injections, and time, answering through return values and HostPort.
class Device {
 public:
  virtual ~Device() {}

  virtual void reset() = 0;

  // The environment attaches its port before any other call.
  void attach(HostPort* port) { port_ = port; }

  // --- Bus operations (override the ones this device supports) ---------
  // I2C: payload only; the address belongs to the binding, not the model.
  virtual uint8_t i2cWrite(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return 2;  // address NACK: this device is not on that bus
  }
  virtual size_t i2cRead(uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return 0;
  }
  virtual uint8_t spiTransfer(uint8_t mosi) {
    (void)mosi;
    return 0xFF;  // idle bus
  }
  // Bytes the application wrote to the device's serial RX.
  virtual void serialIn(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
  }
  // Level changes on the device's logical input lines (chip-select,
  // data/command, ...), forwarded by the environment. Input line ids are
  // a separate space from HostPort output line ids.
  virtual void lineIn(uint8_t line, uint8_t level) {
    (void)line;
    (void)level;
  }
  // A logical protocol frame arriving from the application side — the
  // counterpart of HostPort::frameOut. How the bits are interpreted is
  // the device's business, keyed by the format id; `bus` says which
  // logical link it arrived on.
  virtual void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
                       size_t bits) {
    (void)bus;
    (void)format;
    (void)data;
    (void)bits;
  }

  // --- World -> device injection and inspection -------------------------
  virtual void channelWrite(uint8_t channel, const uint8_t* data,
                            size_t len) {
    (void)channel;
    (void)data;
    (void)len;
  }
  virtual size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) {
    (void)channel;
    (void)out;
    (void)cap;
    return 0;
  }

  // --- Time --------------------------------------------------------------
  // The environment advances the device at its tick boundaries. Latency
  // and periodic output are implemented here, never with platform timers.
  virtual void advanceTo(uint64_t nowUs) { (void)nowUs; }

  // --- Evidence ------------------------------------------------------------
  virtual size_t dump(char* out, size_t cap) {
    (void)out;
    (void)cap;
    return 0;
  }

 protected:
  HostPort* port() const { return port_; }

 private:
  HostPort* port_ = nullptr;
};

}  // namespace ebdev
