// Angle unit model implementation. Pure C++11.
#include "unit_angle_model.h"

#include <stdio.h>

void UnitAngleModel::reset() {
  position_ = 0;
  updates_ = 0;
  present();
}

void UnitAngleModel::present() {
  if (port() == nullptr) return;
  port()->analogOut(kLineAnalog, position_);
  const uint32_t mv = static_cast<uint32_t>(position_) * kFullScaleMv /
                      kFullScale;
  port()->analogOutMilliVolts(kLineAnalog, mv);
}

bool UnitAngleModel::channelWrite(uint8_t channel, const uint8_t* data,
                                  size_t len) {
  if (channel != kChannelAngle || len != 2) return false;
  const uint16_t wanted = static_cast<uint16_t>((data[0] << 8) | data[1]);
  position_ = wanted > kFullScale ? kFullScale : wanted;
  ++updates_;
  present();
  return true;
}

size_t UnitAngleModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel != kChannelAngle) return ebdev::kChannelUnsupported;
  if (cap >= 1) out[0] = static_cast<uint8_t>(position_ >> 8);
  if (cap >= 2) out[1] = static_cast<uint8_t>(position_ & 0xFF);
  return 2;
}

size_t UnitAngleModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "angle pos=%u updates=%u", position_,
                         updates_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
