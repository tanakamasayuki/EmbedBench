// Conformance probe: a device model that checks the environment hosting it
// against the frozen interface contracts, from the device side only. Since
// it needs nothing but embedbench_device.h, any environment — this
// project's two examples or a future one — can be put through the same
// standard scenario and compared by verdict rather than by trace text.
//
// What it can see from where it sits:
//   - re-entrancy: no device method starts while another is running
//   - time: advanceTo is monotonic, repeats are allowed, and nowMicros()
//     inside a call never goes backwards
//   - borrowed buffers: the bytes it was handed match what it copied out
//     during the call
//   - frames: an oversized frame is refused, one within the limit is
//     accepted, padding rules hold
//   - formats: the same name and schema resolve to the same nonzero id,
//     an over-long name is refused
//
// Anything it cannot see from the device side (log shape, event ordering,
// diagnostics) stays the environment's own business.
#pragma once

#include <embedbench_device.h>

class ConformanceProbe : public ebdev::Device {
 public:
  // Each bit is one contract the probe managed to verify. A conforming
  // environment ends the standard scenario with checks == kAllChecks and
  // violations == 0.
  enum Check : uint32_t {
    kCheckNoReentry = 1u << 0,
    kCheckTimeMonotonic = 1u << 1,
    kCheckTimeRepeat = 1u << 2,
    kCheckNowAgrees = 1u << 3,
    kCheckBorrowedBuffer = 1u << 4,
    kCheckFrameAccepted = 1u << 5,
    kCheckFrameOversizeRefused = 1u << 6,
    kCheckFormatStable = 1u << 7,
    kCheckFormatNameLimit = 1u << 8,
  };
  static const uint32_t kAllChecks = (1u << 9) - 1;
  // The contract ALLOWS advanceTo to repeat a time but does not require
  // it, so a repeat is observed when it happens and never demanded. Its
  // semantics (no double firing) are pinned separately in tests/contracts.
  static const uint32_t kRequiredChecks = kAllChecks & ~kCheckTimeRepeat;

  // Channel 0 asks the probe to exercise the port-side checks (frames and
  // formats) at a moment the environment chooses.
  static const uint8_t kChannelProbePort = 0;

  void reset() override;

  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  void serialIn(const uint8_t* data, size_t len) override;
  void lineIn(uint8_t line, uint8_t level) override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

  uint32_t checks() const { return checks_; }
  uint32_t violations() const { return violations_; }
  bool conforms() const {
    return violations_ == 0 && (checks_ & kRequiredChecks) == kRequiredChecks;
  }

 private:
  void enter(const uint8_t* borrowed, size_t len);
  void leave();
  void probePort();

  bool inCall_ = false;
  bool timeSeen_ = false;
  uint64_t lastAdvance_ = 0;
  uint8_t copy_[8] = {0};
  size_t copyLen_ = 0;
  uint32_t checks_ = 0;
  uint32_t violations_ = 0;
  uint32_t calls_ = 0;
};
