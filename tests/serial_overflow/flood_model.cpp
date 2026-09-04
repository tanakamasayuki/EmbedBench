// Oversized-reply model implementation. Pure C++11.
#include "flood_model.h"

#include <stdio.h>

void FloodModel::reset() {
  sent_ = 0;
  refused_ = 0;
}

void FloodModel::serialIn(const uint8_t* data, size_t len) {
  (void)data;
  if (len == 0 || port() == nullptr) return;
  static const char kReply[] = "0123456789AB";
  ++sent_;
  if (!port()->serialOut(reinterpret_cast<const uint8_t*>(kReply),
                         kReplyLength)) {
    // The environment could not take every byte: the contract says the
    // accepted prefix was delivered and the rest was dropped with a
    // diagnostic, so the device knows its reply was truncated.
    ++refused_;
  }
}

size_t FloodModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "flood sent=%u refused=%u", sent_, refused_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
