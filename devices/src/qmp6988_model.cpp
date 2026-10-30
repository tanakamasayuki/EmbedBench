// QMP6988 model implementation. Pure C++11.
#include "qmp6988_model.h"

#include <stdio.h>
#include <string.h>

namespace {

// Power-on contents. A0 is the pilot specimen's calibration (every part
// has its own; channel kChannelCalibration replaces it). D1 and F1-F5 were
// read on the part right after a soft reset; F7 was never read before a
// conversion, so it starts at zeros.
const uint8_t kResetA0[25] = {0x52, 0xDC, 0x0D, 0xA7, 0xF4, 0x82, 0x0D,
                              0xDC, 0x0C, 0xD2, 0x01, 0xE6, 0xFC, 0x86,
                              0xF9, 0xC9, 0xFA, 0xA2, 0xF7, 0x2B, 0x0C,
                              0xBB, 0x03, 0xD8, 0xBB};
const uint8_t kResetD1[1] = {0x5C};
const uint8_t kResetE0[1] = {0x00};
const uint8_t kResetF1[1] = {0x00};
const uint8_t kResetF2[1] = {0x01};
const uint8_t kResetF3[1] = {0x00};
const uint8_t kResetF4[1] = {0x00};
const uint8_t kResetF5[1] = {0x00};
const uint8_t kResetF7[6] = {0, 0, 0, 0, 0, 0};

// The pilot table, with F1, F2 and F5 writable as the datasheet has them
// (the pilot never wrote them).
const RegTableEntry kEntries[9] = {
    {0xA0, 25, kResetA0, 0},
    {0xD1, 1, kResetD1, 0},
    {0xE0, 1, kResetE0, RegTableModel::kWritable},
    {0xF1, 1, kResetF1, RegTableModel::kWritable},
    {0xF2, 1, kResetF2, RegTableModel::kWritable},
    {0xF3, 1, kResetF3, RegTableModel::kVolatile},
    {0xF4, 1, kResetF4, RegTableModel::kWritable},
    {0xF5, 1, kResetF5, RegTableModel::kWritable},
    {0xF7, 6, kResetF7, RegTableModel::kVolatile},
};
// The pilot's 26 data reads all continued a pointer write, which made the
// table generator require a repeated start; M5Unit-ENV reads the
// calibration after a STOP, one byte at a time, and the part answers.
const RegTableSpec kSpec = {kEntries, 9, nullptr, 0, false};

// Samples averaged for a 3-bit oversampling field: skip, 1, 2, ... 64.
uint32_t averaged(uint8_t field) {
  return field == 0 ? 0 : (1u << (field - 1));
}

// IO_SETUP bits 7:5, from the datasheet.
const uint64_t kStandbyUs[8] = {1000,   5000,    50000,   250000,
                                500000, 1000000, 2000000, 4000000};

}  // namespace

Qmp6988Model::Qmp6988Model() : RegTableModel(kSpec) {
  memcpy(calibration_, kResetA0, sizeof(calibration_));
}

void Qmp6988Model::reset() {
  RegTableModel::reset();
  if (calibrationSet_) setBytes(0xA0, calibration_, sizeof(calibration_));
  mode_ = kSleep;
  pending_ = false;
  startUs_ = 0;
  dueUs_ = 0;
  nextSample_ = 0;
  conversions_ = 0;
}

uint64_t Qmp6988Model::now() const {
  return port() != nullptr ? port()->nowMicros() : 0;
}

uint64_t Qmp6988Model::conversionUs() const {
  const uint8_t ctrl = get(kRegCtrlMeas);
  return kConversionBaseUs +
         kConversionPerSampleUs *
             (averaged((ctrl >> 5) & 7) + averaged((ctrl >> 2) & 7));
}

uint64_t Qmp6988Model::normalPeriodUs() const {
  return conversionUs() + kStandbyUs[(get(kRegIoSetup) >> 5) & 7];
}

void Qmp6988Model::complete() {
  if (sampleCount_ > 0) {
    setBytes(kRegData, &samples_[nextSample_ * kSampleBytes], kSampleBytes);
    nextSample_ = (nextSample_ + 1) % sampleCount_;
  }
  ++conversions_;
  if (mode_ == kNormal) {
    startUs_ = dueUs_;
    dueUs_ += normalPeriodUs();
    if (port() != nullptr) port()->requestWake(dueUs_);
  } else {
    // Forced: one conversion, then back to sleep. CTRL_MEAS keeps the
    // value written (the part read back 25 after a forced conversion).
    mode_ = kSleep;
    pending_ = false;
  }
}

void Qmp6988Model::advanceTo(uint64_t nowUs) {
  while (pending_ && nowUs >= dueUs_) complete();
}

void Qmp6988Model::afterWrite(uint8_t address, const uint8_t* value,
                              size_t len) {
  if (len != 1) return;
  if (address == kRegReset) {
    if (value[0] == kResetValue) reset();
    return;
  }
  if (address != kRegCtrlMeas) return;
  const uint8_t bits = value[0] & 3;
  if (bits == 0) {
    mode_ = kSleep;
    pending_ = false;
    return;
  }
  const Mode next = bits == 3 ? kNormal : kForced;
  if (pending_ && mode_ == kForced && next == kNormal) {
    // Normal mode written while a forced conversion runs: that conversion
    // finishes when it would have, and the normal cycle runs on from it
    // (the 400 kHz OEP run: every sample lands on that schedule).
    mode_ = kNormal;
    return;
  }
  mode_ = next;
  pending_ = true;
  startUs_ = now();
  dueUs_ = startUs_ + conversionUs();
  if (port() != nullptr) port()->requestWake(dueUs_);
}

uint8_t Qmp6988Model::byteAt(uint8_t address) const {
  for (size_t back = 0; back <= address && back < 25; ++back) {
    const uint8_t base = static_cast<uint8_t>(address - back);
    if (has(base)) return back < width(base) ? get(base, back) : 0;
  }
  return 0;  // unmapped: 00
}

size_t Qmp6988Model::onRead(uint8_t address, uint8_t* out, size_t len) {
  advanceTo(now());
  if (len == 0) return kPassThrough;
  // Bit 3 is set while a conversion runs, from kStatusDelayUs after it
  // starts: a forced one from the write, in normal mode the last
  // conversionUs() before each sample.
  const uint64_t t = now();
  const uint64_t start =
      mode_ == kNormal && dueUs_ - conversionUs() > startUs_
          ? dueUs_ - conversionUs()
          : startUs_;
  const bool measuring = pending_ && t >= start + kStatusDelayUs;
  set(kRegStatus, 0, static_cast<uint8_t>((get(kRegStatus) & ~kStatusMeasuring) |
                                          (measuring ? kStatusMeasuring : 0)));
  if (has(address) && len <= width(address)) return kPassThrough;
  // Inside a register (A1 is the calibration's second byte) or past its
  // end: the address runs on, as the part's reads do.
  for (size_t k = 0; k < len; ++k) {
    out[k] = byteAt(static_cast<uint8_t>(address + k));
  }
  return len;
}

bool Qmp6988Model::channelWrite(uint8_t channel, const uint8_t* data,
                                size_t len) {
  if (channel == kChannelSamples) {
    if (len == 0 || len % kSampleBytes != 0 ||
        len > sizeof(samples_)) {
      return false;
    }
    memcpy(samples_, data, len);
    sampleCount_ = len / kSampleBytes;
    nextSample_ = 0;
    return true;
  }
  if (channel == kChannelCalibration) {
    if (len != sizeof(calibration_)) return false;
    memcpy(calibration_, data, len);
    calibrationSet_ = true;
    setBytes(0xA0, calibration_, len);
    return true;
  }
  return RegTableModel::channelWrite(channel, data, len);
}

size_t Qmp6988Model::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel == kChannelSamples) {
    const size_t need = sampleCount_ * kSampleBytes;
    if (cap > 0) memcpy(out, samples_, need < cap ? need : cap);
    return need;
  }
  if (channel == kChannelCalibration) {
    if (cap > 0) {
      memcpy(out, calibration_, sizeof(calibration_) < cap
                                    ? sizeof(calibration_)
                                    : cap);
    }
    return sizeof(calibration_);
  }
  return RegTableModel::channelRead(channel, out, cap);
}

size_t Qmp6988Model::dump(char* out, size_t cap) {
  char table[96];
  RegTableModel::dump(table, sizeof(table));
  static const char* const kModes[] = {"sleep", "forced", "normal"};
  return static_cast<size_t>(snprintf(out, cap,
                                       "qmp6988 mode=%s conversions=%u %s",
                                       kModes[mode_],
                                       static_cast<unsigned>(conversions_),
                                       table));
}
