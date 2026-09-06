// Ultrasonic unit model implementation. Pure C++11.
#include "unit_sonic_model.h"

#include <stdio.h>

void UnitSonicModel::reset() {
  rangeMm_ = 0;
  phase_ = Phase::kIdle;
  dueUs_ = 0;
  pulses_ = 0;
  ignored_ = 0;
  // No lineOut here: reset() is effect-free by contract. The echo line
  // rests low until a measurement drives it.
}

bool UnitSonicModel::channelWrite(uint8_t channel, const uint8_t* data,
                                  size_t len) {
  if (channel != kChannelRange || len != 2) return false;
  rangeMm_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
  return true;
}

void UnitSonicModel::lineIn(uint8_t line, uint8_t level) {
  if (line != kLineTrigger || level == 0 || port() == nullptr) return;
  if (phase_ != Phase::kIdle) {
    // A trigger during a measurement is dropped, and the sketch has no
    // way to be told that from a line, so say it out loud.
    ++ignored_;
    port()->diagnose("trigger during measurement");
    return;
  }
  ++pulses_;
  phase_ = Phase::kWaiting;
  dueUs_ = port()->nowMicros() + kStartDelayUs;
  port()->requestWake(dueUs_);
}

void UnitSonicModel::advanceTo(uint64_t nowUs) {
  if (phase_ == Phase::kIdle || nowUs < dueUs_ || port() == nullptr) return;
  if (phase_ == Phase::kWaiting) {
    phase_ = Phase::kEchoing;
    port()->lineOut(kLineEcho, 1);
    dueUs_ = nowUs + static_cast<uint64_t>(rangeMm_) * kUsPerMm;
    port()->requestWake(dueUs_);
  } else {
    phase_ = Phase::kIdle;
    port()->lineOut(kLineEcho, 0);
  }
}

size_t UnitSonicModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "sonic mm=%u pulses=%u ignored=%u",
                         rangeMm_, pulses_, ignored_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
