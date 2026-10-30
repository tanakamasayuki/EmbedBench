// SHT30 model implementation. Pure C++11.
#include "sht30_model.h"

#include <stdio.h>
#include <string.h>

uint8_t Sht30Model::crc8(const uint8_t* data, size_t len) {
  // Datasheet 4.12: polynomial 0x31, initial 0xFF, no reflection.
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = static_cast<uint8_t>((crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1);
    }
  }
  return crc;
}

void Sht30Model::reset() {
  // Power-on: alert pending and reset detected, the datasheet's default.
  status_ = kStatusAlert | kStatusReset;
  pending_ = kNothing;
  holding_ = false;
  stretch_ = false;
  readyAtUs_ = 0;
  nextSample_ = 0;
  measurements_ = 0;
  refused_ = 0;
  stretched_ = 0;
  gaveUp_ = 0;
}

uint64_t Sht30Model::now() const {
  return port() != nullptr ? port()->nowMicros() : 0;
}

bool Sht30Model::busy(uint64_t t) const { return t < readyAtUs_; }

void Sht30Model::start(Repeatability r, bool stretch) {
  static const uint64_t kDuration[3] = {kHighUs, kMediumUs, kLowUs};
  if (pending_ == kMeasurement) {
    // The previous result was never read: the part keeps it and hands it
    // to the next read, even while the new measurement runs (OEP run
    // sht30-ready-idf: a read 4 us after the command got it).
    memcpy(held_, result_, 4);
    holding_ = true;
  }
  if (sampleCount_ > 0) {
    memcpy(result_, &samples_[nextSample_ * 4], 4);
    nextSample_ = (nextSample_ + 1) % sampleCount_;
  }
  pending_ = kMeasurement;
  stretch_ = stretch;
  readyAtUs_ = now() + kDuration[r];
  ++measurements_;
}

uint8_t Sht30Model::i2cWrite(const uint8_t* data, size_t len,
                             const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  const uint64_t t = now();
  if (busy(t)) {
    ++refused_;
    // Measuring, the part refuses its address. Just after a soft reset it
    // takes the address and refuses the command (a write 340 us after
    // 30A2 ended in a data NACK, and nothing was measured).
    return pending_ == kReset ? ebdev::kI2cDataNack : ebdev::kI2cAddressNack;
  }
  if (len == 0) return ebdev::kI2cAck;  // address probe
  if (len != 2) {
    status_ |= kStatusCommand;
    return ebdev::kI2cDataNack;
  }
  const uint16_t command = static_cast<uint16_t>(data[0] << 8 | data[1]);
  status_ &= static_cast<uint16_t>(~kStatusCommand);
  switch (command) {
    case 0x2400: start(kHigh, false); break;
    case 0x240B: start(kMedium, false); break;
    case 0x2416: start(kLow, false); break;
    case 0x2C06: start(kHigh, true); break;
    case 0x2C0D: start(kMedium, true); break;
    case 0x2C10: start(kLow, true); break;
    case 0xF32D: pending_ = kStatus; break;
    case 0x3041: status_ &= static_cast<uint16_t>(~(kStatusAlert | kStatusReset)); break;
    case 0x306D: status_ |= kStatusHeater; break;
    case 0x3066: status_ &= static_cast<uint16_t>(~kStatusHeater); break;
    case 0x3093: pending_ = kNothing; break;  // break: stop periodic mode
    case 0x30A2:
      // Soft reset: back to the power-on status. The OEP run read 8010
      // right after one; the I2CDeviceDB pilot read 0000 (unexplained).
      // Whether an unread result survives it has not been recorded; here
      // it does not.
      status_ = kStatusAlert | kStatusReset;
      pending_ = kReset;
      holding_ = false;
      readyAtUs_ = t + kSoftResetUs;
      break;
    default: {
      status_ |= kStatusCommand;
      char text[64];
      snprintf(text, sizeof(text), "command %04X not modelled", command);
      if (port() != nullptr) port()->diagnose(text);
      return ebdev::kI2cDataNack;
    }
  }
  return ebdev::kI2cAck;
}

size_t Sht30Model::answer(uint8_t* data, size_t len, const uint8_t* words,
                          size_t count) {
  size_t n = 0;
  for (size_t w = 0; w < count && n < len; ++w) {
    const uint8_t word[2] = {words[2 * w], words[2 * w + 1]};
    data[n++] = word[0];
    if (n < len) data[n++] = word[1];
    if (n < len) data[n++] = crc8(word, 2);
  }
  return n;
}

size_t Sht30Model::i2cRead(uint8_t* data, size_t len,
                           const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  const uint64_t t = now();
  if (busy(t) && holding_ && len > 0) {
    holding_ = false;
    return answer(data, len, held_, 2);
  }
  if (busy(t)) {
    if (pending_ != kMeasurement || !stretch_) {
      ++refused_;
      return 0;  // the address is refused while the part is busy
    }
    // Clock stretching: the part holds SCL until the result is ready.
    if (stretchLimitUs_ != 0 && readyAtUs_ - t > stretchLimitUs_) {
      ++gaveUp_;
      return 0;  // the master gave up; the result stays for a later read
    }
    ++stretched_;
  }
  if (len == 0) return 0;
  if (pending_ == kMeasurement) {
    pending_ = kNothing;
    holding_ = false;
    return answer(data, len, result_, 2);
  }
  if (pending_ == kStatus) {
    const uint8_t word[2] = {static_cast<uint8_t>(status_ >> 8),
                             static_cast<uint8_t>(status_)};
    pending_ = kNothing;
    return answer(data, len, word, 1);
  }
  return 0;  // nothing to read: the part does not acknowledge
}

bool Sht30Model::channelWrite(uint8_t channel, const uint8_t* data,
                              size_t len) {
  if (channel == kChannelSamples) {
    if (len == 0 || len % 4 != 0 || len > sizeof(samples_)) return false;
    memcpy(samples_, data, len);
    sampleCount_ = len / 4;
    nextSample_ = 0;
    return true;
  }
  if (channel == kChannelStretchLimit) {
    if (len != 4) return false;
    stretchLimitUs_ = static_cast<uint32_t>(data[0]) << 24 |
                      static_cast<uint32_t>(data[1]) << 16 |
                      static_cast<uint32_t>(data[2]) << 8 | data[3];
    return true;
  }
  return false;
}

size_t Sht30Model::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel == kChannelSamples) {
    const size_t need = sampleCount_ * 4;
    if (cap > 0) memcpy(out, samples_, need < cap ? need : cap);
    return need;
  }
  if (channel == kChannelStretchLimit) {
    const uint8_t value[4] = {static_cast<uint8_t>(stretchLimitUs_ >> 24),
                              static_cast<uint8_t>(stretchLimitUs_ >> 16),
                              static_cast<uint8_t>(stretchLimitUs_ >> 8),
                              static_cast<uint8_t>(stretchLimitUs_)};
    if (cap > 0) memcpy(out, value, cap < 4 ? cap : 4);
    return 4;
  }
  return ebdev::kChannelUnsupported;
}

size_t Sht30Model::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "sht30 status=%04X measurements=%u refused=%u "
                         "stretched=%u gave_up=%u",
                         status_, measurements_, refused_, stretched_, gaveUp_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
