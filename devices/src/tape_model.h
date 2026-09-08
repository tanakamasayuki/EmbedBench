// A device that plays back a captured session. The tape is the script a
// real part was seen to follow: the writes it acknowledged, the bytes it
// answered, the serial replies it sent and when. The model answers each
// application request from the next step and says, through the diagnostic
// path, exactly where the application left the recording — a different
// payload, a different length, a request the recording does not have.
//
// This is the first thing to run after taking a capture: does the
// application on the host walk the same path it walked on the board? It
// cannot branch (a tape has no state beyond its position), which is why
// it is the complement of regtable_model rather than a replacement (X64).
//
// Steps the application initiates (kWrite, kRead, kSerialIn) wait for it;
// steps the device initiates (kSerialOut) are performed on their own once
// they become current, `delayUs` after the previous step completed. A
// device-initiated first step is armed at the first advanceTo(), since
// reset() may not call HostPort. Pure C++11.
#pragma once

#include <embedbench_device.h>

struct TapeStep {
  uint8_t kind;         // TapeModel::kWrite / kRead / kSerialIn / kSerialOut
  uint8_t status;       // kWrite: the I2cStatus to answer
  uint8_t stop;         // kWrite / kRead: the recorded STOP bit, checked
  uint8_t length;       // bytes in `data`
  uint8_t request;      // kRead: bytes the application asked for (0 = unchecked)
  const uint8_t* data;  // kWrite / kSerialIn: expected; kRead / kSerialOut: answer
  uint32_t delayUs;     // kSerialOut: after the previous step completed
};

struct TapeSpec {
  const TapeStep* steps;
  size_t count;
};

class TapeModel : public ebdev::Device {
 public:
  enum Kind : uint8_t { kWrite = 1, kRead = 2, kSerialIn = 3, kSerialOut = 4 };

  explicit TapeModel(const TapeSpec& spec) : spec_(&spec) {}

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  void serialIn(const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

  size_t position() const { return step_; }
  bool finished() const { return step_ >= spec_->count; }
  uint32_t mismatches() const { return mismatches_; }

 private:
  const TapeStep* current() const;
  void finishStep();
  void arm();
  void mismatch(const char* fmt, ...);
  static const char* kindName(uint8_t kind);

  const TapeSpec* spec_;
  size_t step_ = 0;
  bool armed_ = false;
  uint64_t dueAtUs_ = 0;
  size_t serialGot_ = 0;
  bool serialBad_ = false;
  bool exhaustedSaid_ = false;
  uint32_t mismatches_ = 0;
};
