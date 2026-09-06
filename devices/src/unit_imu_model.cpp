// IMU FIFO implementation. Pure C++11.
#include "unit_imu_model.h"

#include <stdio.h>

void UnitImuModel::reset() {
  reg_ = 0;
  sampling_ = false;
  nextSampleUs_ = 0;
  value_ = 0;
  for (size_t i = 0; i < kFifoDepth; ++i) fifo_[i] = 0;
  count_ = 0;
  flags_ = 0;
  irq_ = 0;
  taken_ = 0;
  lost_ = 0;
}

void UnitImuModel::updateIrq() {
  // The line follows the watermark in both directions: it goes up when
  // the FIFO reaches it and comes back down when a read drains past it.
  const uint8_t want = count_ >= kWatermark ? 1 : 0;
  if (want == irq_) return;
  irq_ = want;
  if (port() != nullptr) port()->lineOut(kLineIrq, irq_);
}

uint8_t UnitImuModel::i2cWrite(const uint8_t* data, size_t len,
                               const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 0) return ebdev::kI2cAddressNack;
  reg_ = data[0];
  if (reg_ == kRegControl && len >= 2) {
    const bool wanted = data[1] != 0;
    if (wanted && !sampling_ && port() != nullptr) {
      // Sampling starts from now, not from some boundary the environment
      // happens to reach next.
      nextSampleUs_ = port()->nowMicros() + kSampleUs;
      port()->requestWake(nextSampleUs_);
    }
    sampling_ = wanted;
    return ebdev::kI2cAck;
  }
  if (reg_ != kRegStatus && reg_ != kRegFifo && reg_ != kRegControl) {
    if (port() != nullptr) port()->diagnose("write to unmapped register");
    return ebdev::kI2cDataNack;
  }
  return ebdev::kI2cAck;
}

size_t UnitImuModel::i2cRead(uint8_t* data, size_t len,
                             const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (reg_ == kRegStatus) {
    if (len < 2) return 0;
    data[0] = static_cast<uint8_t>(count_);
    data[1] = flags_;
    flags_ = 0;  // reading the status clears the sticky overflow bit
    return 2;
  }
  if (reg_ != kRegFifo) return 0;
  // How much comes back is the FIFO's business: the caller asks for as
  // much as it can hold and gets what is actually there.
  size_t samples = len / 2;
  if (samples > count_) samples = count_;
  for (size_t i = 0; i < samples; ++i) {
    data[i * 2] = static_cast<uint8_t>(fifo_[i] >> 8);
    data[i * 2 + 1] = static_cast<uint8_t>(fifo_[i] & 0xFF);
  }
  for (size_t i = samples; i < count_; ++i) fifo_[i - samples] = fifo_[i];
  count_ -= samples;
  taken_ += static_cast<uint32_t>(samples);
  updateIrq();
  return samples * 2;
}

void UnitImuModel::advanceTo(uint64_t nowUs) {
  if (!sampling_) return;
  // A jump delivers every sample that fell in it, which is how the FIFO
  // comes to overflow when the application was away for a long while.
  while (nowUs >= nextSampleUs_) {
    if (count_ < kFifoDepth) {
      fifo_[count_++] = value_;
    } else {
      ++lost_;
      if ((flags_ & kFlagOverflow) == 0) {
        flags_ |= kFlagOverflow;
        if (port() != nullptr) port()->diagnose("fifo overflow: samples lost");
      }
    }
    nextSampleUs_ += kSampleUs;
  }
  updateIrq();
  if (port() != nullptr) port()->requestWake(nextSampleUs_);
}

bool UnitImuModel::channelWrite(uint8_t channel, const uint8_t* data,
                                size_t len) {
  if (channel != kChannelValue || len < 2) return false;
  value_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
  return true;
}

size_t UnitImuModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "imu on=%u count=%u taken=%u lost=%u irq=%u",
                         sampling_ ? 1u : 0u, static_cast<unsigned>(count_),
                         taken_, lost_, irq_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
