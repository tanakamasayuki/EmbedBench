// RTC implementation. Pure C++11.
#include "unit_rtc_model.h"

#include <stdio.h>

namespace {
const uint64_t kUsPerSecond = 1000000;
}

void UnitRtcModel::reset() {
  reg_ = 0;
  based_ = false;
  baseSec_ = 0;
  baseUs_ = 0;
  alarmSec_ = 0;
  alarmSet_ = false;
  fired_ = false;
  rejected_ = 0;
}

uint32_t UnitRtcModel::nowSeconds() const {
  if (!based_ || port() == nullptr) return baseSec_;
  const uint64_t elapsed = port()->nowMicros() - baseUs_;
  return baseSec_ + static_cast<uint32_t>(elapsed / kUsPerSecond);
}

void UnitRtcModel::armAlarm() {
  if (!alarmSet_ || !based_ || port() == nullptr) return;
  // The device's unit is seconds and the environment's is microseconds;
  // converting between them is this model's job, not the interface's.
  const uint64_t whenUs =
      baseUs_ + static_cast<uint64_t>(alarmSec_ - baseSec_) * kUsPerSecond;
  port()->requestWake(whenUs);
}

uint8_t UnitRtcModel::i2cWrite(const uint8_t* data, size_t len,
                               const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 0) return ebdev::kI2cAddressNack;
  reg_ = data[0];
  if (reg_ == kRegTime && len >= 5) {
    baseSec_ = (static_cast<uint32_t>(data[1]) << 24) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 8) |
               static_cast<uint32_t>(data[4]);
    // The base can only be taken now: attach hands over the port and
    // nothing more, and reset is not allowed to ask the port anything.
    baseUs_ = port() != nullptr ? port()->nowMicros() : 0;
    based_ = true;
    armAlarm();
    return ebdev::kI2cAck;
  }
  if (reg_ == kRegAlarm && len >= 5) {
    const uint32_t when = (static_cast<uint32_t>(data[1]) << 24) |
                          (static_cast<uint32_t>(data[2]) << 16) |
                          (static_cast<uint32_t>(data[3]) << 8) |
                          static_cast<uint32_t>(data[4]);
    if (based_ && when <= nowSeconds()) {
      // An alarm in the past would never fire; say so rather than let the
      // application wait for something that cannot happen.
      ++rejected_;
      if (port() != nullptr) port()->diagnose("alarm time already past");
      return ebdev::kI2cDataNack;
    }
    alarmSec_ = when;
    alarmSet_ = true;
    fired_ = false;
    armAlarm();
    return ebdev::kI2cAck;
  }
  if (reg_ != kRegTime && reg_ != kRegAlarm && reg_ != kRegStatus) {
    if (port() != nullptr) port()->diagnose("write to unmapped register");
    return ebdev::kI2cDataNack;
  }
  return ebdev::kI2cAck;
}

size_t UnitRtcModel::i2cRead(uint8_t* data, size_t len,
                             const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (reg_ == kRegTime) {
    if (len < 4) return 0;
    const uint32_t now = nowSeconds();
    data[0] = static_cast<uint8_t>(now >> 24);
    data[1] = static_cast<uint8_t>(now >> 16);
    data[2] = static_cast<uint8_t>(now >> 8);
    data[3] = static_cast<uint8_t>(now);
    return 4;
  }
  if (reg_ == kRegStatus) {
    if (len < 1) return 0;
    data[0] = fired_ ? 1 : 0;
    return 1;
  }
  return 0;
}

void UnitRtcModel::advanceTo(uint64_t nowUs) {
  (void)nowUs;
  if (!alarmSet_ || fired_ || !based_) return;
  if (nowSeconds() < alarmSec_) return;
  fired_ = true;
  alarmSet_ = false;
  if (port() != nullptr) port()->lineOut(kLineInt, 1);
}

size_t UnitRtcModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "rtc sec=%u alarm=%u fired=%u rejected=%u",
                         nowSeconds(), alarmSec_, fired_ ? 1u : 0u, rejected_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
