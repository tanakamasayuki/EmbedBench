// EmbedBench device interface — the portable boundary between device
// models and any environment that hosts them.
//
// This is the one surface the project intends to fix: a device model
// written against this header must compile and behave identically as pure
// C++11 with no platform at all, on the Arduino host core, or on any
// future environment. Everything above it (how an environment owns host
// hooks, drives a clock, or records events) is treated as a per-platform
// implementation example — see src/embedbench_draft.* for the
// host-arduino-core one and tests/common_env/ for a pure native one.
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
// Contracts every environment must honor (devices may rely on them):
//
//   Re-entrancy. The environment never re-enters a Device: while ANY
//   Device method (bus operation, lineIn, frameIn, channel access,
//   advanceTo, dump) is on the call stack, effects that the device's
//   HostPort calls trigger elsewhere — interrupt handlers, application
//   callbacks receiving frames or bytes, other devices — are recorded at
//   once but DELIVERED only after the outermost Device method returns.
//   A device therefore needs no re-entrancy guards and may call HostPort
//   freely from any method. Deferred delivery is bounded by the
//   environment's deferral capacity; an effect beyond it is recorded as a
//   diagnostic and dropped — never delivered re-entrantly, never lost in
//   silence.
//
//   Time. The nowUs passed to advanceTo() is monotonic non-decreasing
//   between two reset() calls and may repeat the same value; a device
//   must not emit a due event twice. A jump past several due times must
//   deliver all due behavior at that call, in due order (the environment
//   stamps them all with nowUs). Inside any Device method nowMicros()
//   returns the environment's current time, never less than the last
//   advanceTo() argument. reset() must drop every pending due time; the
//   environment may restart its clock afterwards.
//
//   Borrowed buffers. Every pointer passed into a Device method is valid
//   for the duration of that call only; the device copies what it keeps.
//   Output buffers (channelRead, dump) are non-null whenever cap > 0.
//
//   Ownership of links. A Device owns at most one I2C endpoint, one SPI
//   slave role, and one serial port; composite hardware with several is
//   composed by the environment's adapter from child Devices. Lines,
//   channels, and frame buses are numbered per device.
//
// Still provisional while the draft phase runs, but this file is the
// candidate being hardened, not an implementation detail.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ebdev {

// I2C write status as Arduino's Wire::endTransmission() reports it. A
// device's i2cWrite() must return one of these five values; anything
// else is a contract violation an environment may diagnose.
enum I2cStatus : uint8_t {
  kI2cAck = 0,
  kI2cDataTooLong = 1,
  kI2cAddressNack = 2,
  kI2cDataNack = 3,
  kI2cOther = 4,
};

// Transaction context of one I2C transfer, as the bus master issued it.
// `continued` is a property of the BUS, not of the device: it is true
// only when the immediately preceding transfer on that bus went to this
// same device and ended without STOP. A transfer to any other address in
// between closes the sequence.
struct I2cTransfer {
  // A STOP condition follows this transfer. false = the master intends to
  // continue with another transfer to this device (repeated start).
  bool stop;
  // This transfer follows a transfer to this device that ended without
  // STOP, i.e. it was issued under a repeated start. The classic
  // "write register pointer, then read" sequence arrives as a write with
  // stop=false followed by a read with continued=true.
  bool continued;
};

// Longest format name an environment must accept, in characters. Longer
// names are refused whole (formatId returns 0 with a diagnostic), never
// truncated, so "same name, same id" can never be broken by clipping.
constexpr size_t kFormatNameMaxLength = 19;

// channelRead() result meaning "this device has no such channel", as
// opposed to 0, which is a valid empty value.
constexpr size_t kChannelUnsupported = static_cast<size_t>(-1);

// Frame bit packing (frameIn / frameOut). Bits are packed MSB-first:
// frame bit 0 is bit 7 of data[0], frame bit 8 is bit 7 of data[1], and
// so on; the unused low bits of the last byte MUST be zero — senders
// clear them and environments reject a frame with non-zero padding as a
// contract violation. `data` holds frameBytes(bits) bytes. bits == 0 is a
// valid empty frame (a trigger with no payload) and `data` may then be
// nullptr. How the bits are interpreted belongs to the format.
inline size_t frameBytes(size_t bits) {
  return bits / 8 + (bits % 8 != 0 ? 1 : 0);  // no overflow for any bits
}
inline bool framePaddingClean(const uint8_t* data, size_t bits) {
  if (bits == 0) return true;
  if (data == nullptr) return false;
  if (bits % 8 == 0) return true;
  const uint8_t mask = static_cast<uint8_t>((1u << (8 - bits % 8)) - 1u);
  return (data[frameBytes(bits) - 1] & mask) == 0;
}

// Recommended schema fingerprint for formatId(): FNV-1a over a short text
// describing the frame layout, e.g. schemaFingerprint("u8 addr,u8 cmd").
// The fingerprint is caller-chosen; this helper just makes the common
// choice deterministic and portable. The uint32_t width is fixed.
inline uint32_t schemaFingerprint(const char* layout) {
  uint32_t hash = 2166136261u;
  for (const char* p = layout; p != nullptr && *p != '\0'; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash;
}

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

  // Send bytes toward the application (the device's serial TX). Any byte
  // value, including NUL, is carried. Call boundaries carry no meaning:
  // the receiver sees one byte stream.
  virtual void serialOut(const uint8_t* data, size_t len) = 0;

  // Emit one logical protocol frame toward the application side: the bits
  // as they exist before physical encoding, plus a format id naming how
  // to interpret them. This is the extension path for protocols the
  // interface has no dedicated port for (PIO, IR, WS2812, ...): by
  // project policy the physical layer is never reproduced, so frames —
  // not pin-level bit-banging — are how such devices talk. `bus` is the
  // device-local logical link the frame travels on.
  //
  // Frames are one-way. If a protocol needs request/response correlation
  // it lives inside the format's payload; the interface has no
  // frame-level linking and will not grow one (decided).
  //
  // Atomic: the frame is delivered whole or not at all. Returns true when
  // accepted. Returns false — and the environment records a diagnostic —
  // when frames are not routed, the format id is 0 or not registered in
  // this environment, the frame exceeds maxFrameBits(bus), or the padding
  // is not clean. A device never splits a frame to fit: only a format that
  // itself defines segmentation may emit several frames, each a complete
  // frame of that format.
  virtual bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                        size_t bits) {
    (void)bus;
    (void)format;
    (void)data;
    (void)bits;
    return false;
  }

  // Resolve a protocol format to this environment's id for it. The NAME
  // is the cross-library identity and follows `<vendor>.<protocol>.<ver>`
  // (e.g. "acme.thermo.1"), at most kFormatNameMaxLength characters; the
  // environment interns it, so the same name yields the same nonzero id
  // within an environment while the number stays environment-local.
  // `schema` is a caller-chosen fingerprint of the frame layout (see
  // schemaFingerprint): a registration whose name is already known with a
  // different schema is a conflict — the environment returns 0 and
  // records a diagnostic — which catches two libraries that picked the
  // same name for different layouts. 0 also means no frame routing, a
  // full registry, or a name over the length limit; a device holding id 0
  // must match no frame. Devices resolve once and cache; every frame call
  // then compares integers.
  virtual uint16_t formatId(const char* name, uint32_t schema) {
    (void)name;
    (void)schema;
    return 0;
  }

  // The largest frame this environment delivers atomically on one
  // logical bus, in bits. Limits are negotiated, never fixed in this
  // header: they are an environment property (record buffers, transport
  // MTUs), not a protocol property. 0 means frames are not routed. A
  // frame within the limit is guaranteed to be accepted whole; a larger
  // one is refused by frameOut (see there) rather than truncated.
  virtual uint32_t maxFrameBits(uint8_t bus) {
    (void)bus;
    return 0;
  }
};

// A device model: a deterministic state machine fed by bus operations,
// injections, and time, answering through return values and HostPort.
class Device {
 public:
  virtual ~Device() {}

  // Return to power-on state and drop every pending due time.
  virtual void reset() = 0;

  // The environment attaches its port before any other call.
  void attach(HostPort* port) { port_ = port; }

  // --- Bus operations (override the ones this device supports) ---------
  // I2C: payload only; the address belongs to the binding, not the model.
  // The return value is the I2cStatus (0-4) the master sees.
  virtual uint8_t i2cWrite(const uint8_t* data, size_t len,
                           const I2cTransfer& xfer) {
    (void)data;
    (void)len;
    (void)xfer;
    return kI2cAddressNack;  // this device is not on that bus
  }
  // Fill up to `len` bytes; return how many were supplied (0 = nothing,
  // which the master sees as a failed read).
  virtual size_t i2cRead(uint8_t* data, size_t len, const I2cTransfer& xfer) {
    (void)data;
    (void)len;
    (void)xfer;
    return 0;
  }
  // SPI slave: one byte in, one byte out, qualified by any lineIn state
  // (chip-select, data/command) the device tracks.
  virtual uint8_t spiTransfer(uint8_t mosi) {
    (void)mosi;
    return 0xFF;  // idle bus
  }
  // Bytes the application wrote to the device's serial RX. This is a byte
  // STREAM: call boundaries carry no meaning — "AT" in one call and "A"
  // then "T" in two calls are the same input — so a device buffers and
  // parses its protocol itself (terminators, lengths) and never keys its
  // behavior on `len`.
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
  // counterpart of HostPort::frameOut, same packing rules, always whole.
  virtual void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
                       size_t bits) {
    (void)bus;
    (void)format;
    (void)data;
    (void)bits;
  }

  // --- World -> device injection and inspection -------------------------
  // Apply an injected value. Returns true only when the whole payload was
  // applied; false for an unsupported channel or a wrong length, which the
  // environment records as a diagnostic rather than a silent no-op.
  virtual bool channelWrite(uint8_t channel, const uint8_t* data,
                            size_t len) {
    (void)channel;
    (void)data;
    (void)len;
    return false;
  }
  // snprintf-style: returns the length the channel needs and writes
  // min(needed, cap) bytes into `out` (non-null when cap > 0). A return
  // value larger than `cap` means truncation; 0 is a valid empty value;
  // kChannelUnsupported means the device has no such channel.
  virtual size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) {
    (void)channel;
    (void)out;
    (void)cap;
    return kChannelUnsupported;
  }

  // --- Time --------------------------------------------------------------
  // The environment advances the device at its tick boundaries under the
  // time contract above. Latency and periodic output are implemented
  // here, never with platform timers.
  virtual void advanceTo(uint64_t nowUs) { (void)nowUs; }

  // --- Evidence ------------------------------------------------------------
  // snprintf-style: returns the length of the full text (excluding the
  // terminating NUL), writes at most cap - 1 characters into `out`
  // (non-null when cap > 0) and always NUL-terminates when cap > 0. A
  // return value >= cap means truncation.
  virtual size_t dump(char* out, size_t cap) {
    if (cap > 0) out[0] = '\0';
    return 0;
  }

 protected:
  HostPort* port() const { return port_; }

 private:
  HostPort* port_ = nullptr;
};

}  // namespace ebdev
