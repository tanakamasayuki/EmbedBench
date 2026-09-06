// PIR unit model implementation. Pure C++11.
#include "unit_pir_model.h"

#include <stdio.h>

void UnitPirModel::reset() {
  asserted_ = false;
  releaseAtUs_ = 0;
  triggers_ = 0;
  // No lineOut here: reset() is effect-free by contract. The line rests
  // low until this sensor drives it, which is the environment's initial
  // pin state rather than something the model announces.
}

bool UnitPirModel::channelWrite(uint8_t channel, const uint8_t* data,
                                size_t len) {
  if (channel != kChannelMotion || len != 1 || port() == nullptr) return false;
  if (data[0] == 0) return true;  // motion ending does not drop the line
  ++triggers_;
  releaseAtUs_ = port()->nowMicros() + kHoldUs;
  port()->requestWake(releaseAtUs_);
  if (!asserted_) {
    asserted_ = true;
    port()->lineOut(kLineSignal, 1);
  }
  return true;
}

void UnitPirModel::advanceTo(uint64_t nowUs) {
  if (asserted_ && nowUs >= releaseAtUs_) {
    asserted_ = false;
    if (port() != nullptr) port()->lineOut(kLineSignal, 0);
  }
}

size_t UnitPirModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "pir asserted=%u triggers=%u",
                         asserted_ ? 1 : 0, triggers_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
