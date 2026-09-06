// Environmental sensor model implementation. Pure C++11.
#include "env_sensor_model.h"

#include <stdio.h>

void EnvSensorModel::reset() {
  pointer_ = 0;
  ctrl_ = 0;
  measuring_ = false;
  readyAtUs_ = 0;
  rawTemp_ = 0;
  latched_ = 0;
  measurements_ = 0;
  badRegisters_ = 0;
}

uint8_t EnvSensorModel::i2cWrite(const uint8_t* data, size_t len,
                                 const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 1) {
    pointer_ = data[0];
    return ebdev::kI2cAck;
  }
  if (len == 2 && data[0] == kRegCtrl) {
    pointer_ = kRegCtrl;
    ctrl_ = data[1];
    if (ctrl_ == kCmdForced && !measuring_ && port() != nullptr) {
      // A forced measurement takes a fixed time the datasheet states.
      measuring_ = true;
      readyAtUs_ = port()->nowMicros() + kMeasureUs;
      port()->requestWake(readyAtUs_);
    }
    return ebdev::kI2cAck;
  }
  return ebdev::kI2cDataNack;
}

size_t EnvSensorModel::i2cRead(uint8_t* data, size_t len,
                               const ebdev::I2cTransfer& xfer) {
  // Like the real part, a read must follow a pointer write under a
  // repeated start; a bare read has no register to answer from.
  if (!xfer.continued) return 0;
  switch (pointer_) {
    case kRegChipId:
      if (len < 1) return 0;
      data[0] = kChipId;
      return 1;
    case kRegStatus:
      if (len < 1) return 0;
      data[0] = measuring_ ? kStatusMeasuring : 0;
      return 1;
    case kRegTemp:
      if (len < 3) return 0;
      data[0] = static_cast<uint8_t>((latched_ >> 12) & 0xFF);
      data[1] = static_cast<uint8_t>((latched_ >> 4) & 0xFF);
      data[2] = static_cast<uint8_t>((latched_ << 4) & 0xF0);
      return 3;
    default:
      // No return value can express "there is no such register", so the
      // sensor says it through the diagnostic path.
      ++badRegisters_;
      if (port() != nullptr) port()->diagnose("read of unmapped register");
      return 0;
  }
}

bool EnvSensorModel::channelWrite(uint8_t channel, const uint8_t* data,
                                  size_t len) {
  if (channel != kChannelTemp || len != 3) return false;
  rawTemp_ = static_cast<int32_t>((static_cast<uint32_t>(data[0]) << 16) |
                                  (static_cast<uint32_t>(data[1]) << 8) |
                                  data[2]);
  return true;
}

size_t EnvSensorModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel != kChannelTemp) return ebdev::kChannelUnsupported;
  if (cap >= 1) out[0] = static_cast<uint8_t>((latched_ >> 16) & 0xFF);
  if (cap >= 2) out[1] = static_cast<uint8_t>((latched_ >> 8) & 0xFF);
  if (cap >= 3) out[2] = static_cast<uint8_t>(latched_ & 0xFF);
  return 3;
}

void EnvSensorModel::advanceTo(uint64_t nowUs) {
  if (measuring_ && nowUs >= readyAtUs_) {
    measuring_ = false;
    latched_ = rawTemp_;
    ++measurements_;
    ctrl_ = 0;  // the part returns to sleep mode after a forced measurement
    if (port() != nullptr) port()->lineOut(kLineDataReady, 1);
  }
}

size_t EnvSensorModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "env raw=%06X latched=%06X meas=%u bad_reg=%u",
                         static_cast<unsigned>(rawTemp_ & 0xFFFFFF),
                         static_cast<unsigned>(latched_ & 0xFFFFFF),
                         measurements_, badRegisters_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
