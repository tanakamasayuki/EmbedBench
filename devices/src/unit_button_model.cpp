// Button unit model implementation. Pure C++11.
#include "unit_button_model.h"

#include <stdio.h>

void UnitButtonModel::reset() {
  pressed_ = false;
  presses_ = 0;
  // Idle is high: the pull-up wins while nobody is pressing.
  if (port() != nullptr) port()->lineOut(kLineSignal, 1);
}

bool UnitButtonModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  if (channel != kChannelPress || len != 1) return false;
  const bool now = data[0] != 0;
  if (now && !pressed_) ++presses_;
  pressed_ = now;
  if (port() != nullptr) port()->lineOut(kLineSignal, pressed_ ? 0 : 1);
  return true;
}

size_t UnitButtonModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel != kChannelPress) return ebdev::kChannelUnsupported;
  if (cap >= 1) out[0] = pressed_ ? 1 : 0;
  return 1;
}

size_t UnitButtonModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "button pressed=%u presses=%u",
                         pressed_ ? 1 : 0, presses_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
