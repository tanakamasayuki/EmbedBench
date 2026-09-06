// Relay unit model implementation. Pure C++11.
#include "unit_relay_model.h"

#include <stdio.h>

void UnitRelayModel::reset() {
  closed_ = false;
  everSwitched_ = false;
  lastSwitchUs_ = 0;
  switches_ = 0;
  chatter_ = 0;
}

void UnitRelayModel::lineIn(uint8_t line, uint8_t level) {
  if (line != kLineControl) return;
  const bool wanted = level != 0;
  if (wanted == closed_) return;  // holding the line steady is not a switch
  const uint64_t now = port() != nullptr ? port()->nowMicros() : 0;
  if (everSwitched_ && now - lastSwitchUs_ < kSettleUs) {
    ++chatter_;
    if (port() != nullptr) port()->diagnose("switched before contacts settled");
  }
  closed_ = wanted;
  everSwitched_ = true;
  lastSwitchUs_ = now;
  ++switches_;
}

size_t UnitRelayModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel != kChannelState) return ebdev::kChannelUnsupported;
  if (cap >= 1) out[0] = closed_ ? 1 : 0;
  return 1;
}

size_t UnitRelayModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "relay closed=%u switches=%u chatter=%u",
                         closed_ ? 1 : 0, switches_, chatter_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
