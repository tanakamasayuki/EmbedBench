// QMP6988 barometric pressure sensor (QST), as on the M5Stack ENV III
// (U001-C, 0x70). The first model built from a real part's recording
// (REAL_DEVICE_ROADMAP L1): the register table and its power-on contents
// come from trace2regtable.py over the I2CDeviceDB pilot capture, and the
// hooks are the behaviour that capture showed and the table could not say.
//
//   - A write of CTRL_MEAS (F4) with a forced mode sets DEVICE_STAT (F3)
//     bit 3 for the conversion time, then the six data bytes (F7) take the
//     next sample. CTRL_MEAS reads back what was written, forced mode bits
//     and all, after the conversion (seen on the part).
//   - Normal mode converts once per conversion time + standby (IO_SETUP,
//     as measured: 5.39, 44.6 and 212.0 ms for the nominal 5, 50, 250);
//     F3 bit 3 is set during each conversion.
//   - Reads are byte-addressed: a pointer inside a register (A1, the
//     calibration's second byte) answers from there, and a read longer than
//     the register runs on into the next addresses (F3 for three bytes is
//     F3, F4, F5). No repeated start is needed. Both seen on the part.
//   - RESET (E0) = E6 returns every register to its power-on contents.
//
// What the data bytes say is the world's business: channel kChannelSamples
// takes a sequence of raw six-byte samples (pressure then temperature, as
// F7..FC read), one consumed per conversion, the sequence repeating.
// Pure C++11.
#pragma once

#include <regtable_model.h>

class Qmp6988Model : public RegTableModel {
 public:
  static const uint8_t kRegReset = 0xE0;
  static const uint8_t kRegStatus = 0xF3;
  static const uint8_t kRegCtrlMeas = 0xF4;
  static const uint8_t kRegIoSetup = 0xF5;
  static const uint8_t kRegData = 0xF7;
  static const uint8_t kResetValue = 0xE6;
  static const uint8_t kStatusMeasuring = 0x08;
  static const size_t kSampleBytes = 6;
  static const size_t kMaxSamples = 32;

  static const uint8_t kChannelSamples = 0;  // n x 6 raw bytes
  static const uint8_t kChannelCalibration = 1;  // the 25 bytes at A0

  // Conversion time, linear in the number of samples averaged: through the
  // datasheet's 5.5 ms (P x2, T x1) and 33.7 ms (P x32, T x4). It gives
  // 4.65 ms for P x1, T x1, inside the 4.13-4.84 ms the pilot measured.
  // Physical.
  static const uint64_t kConversionBaseUs = 2940;
  // Physical (same fit).
  static const uint64_t kConversionPerSampleUs = 855;

  // From the start of a conversion to DEVICE_STAT bit 3 reading set,
  // measured as a model sees it: from the start of the CTRL_MEAS write to
  // the start of the status read. A 400 kHz sweep in 20 us steps read 00
  // up to 216 us and 08 from 234 us; at 100 kHz the earliest read (550 us)
  // already had it. Physical.
  static const uint64_t kStatusDelayUs = 225;

  Qmp6988Model();

  void reset() override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

  // Effect-free: what the current CTRL_MEAS and IO_SETUP make of time.
  uint64_t conversionUs() const;
  uint64_t normalPeriodUs() const;

 protected:
  size_t onRead(uint8_t address, uint8_t* out, size_t len) override;
  void afterWrite(uint8_t address, const uint8_t* value, size_t len) override;

 private:
  enum Mode : uint8_t { kSleep, kForced, kNormal };
  void complete();
  uint8_t byteAt(uint8_t address) const;
  uint64_t now() const;

  Mode mode_ = kSleep;
  bool pending_ = false;
  uint64_t startUs_ = 0;  // when the conversion cycle in progress began
  uint64_t dueUs_ = 0;    // when the conversion in progress completes
  uint8_t samples_[kMaxSamples * kSampleBytes];
  size_t sampleCount_ = 0;
  size_t nextSample_ = 0;
  uint8_t calibration_[25];
  bool calibrationSet_ = false;
  uint32_t conversions_ = 0;
};
