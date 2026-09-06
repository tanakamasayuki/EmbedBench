// Light unit model implementation. Pure C++11.
#include "unit_light_model.h"

#include <stdio.h>

void UnitLightModel::reset() {
  brightness_ = 0;
  threshold_ = kDefaultThreshold;
  dark_ = false;
  trips_ = 0;
  present();
}

void UnitLightModel::present() {
  if (port() == nullptr) return;
  port()->analogOut(kLineAnalog, brightness_);
  const bool dark = brightness_ < threshold_;
  if (dark != dark_) {
    dark_ = dark;
    ++trips_;
    port()->lineOut(kLineDigital, dark_ ? 1 : 0);
  }
}

bool UnitLightModel::channelWrite(uint8_t channel, const uint8_t* data,
                                  size_t len) {
  if (len != 2) return false;
  const uint16_t value = static_cast<uint16_t>((data[0] << 8) | data[1]);
  if (channel == kChannelBrightness) {
    brightness_ = value;
  } else if (channel == kChannelThreshold) {
    threshold_ = value;
  } else {
    return false;
  }
  present();
  return true;
}

size_t UnitLightModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "light lux=%u thr=%u dark=%u trips=%u",
                         brightness_, threshold_, dark_ ? 1 : 0, trips_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
