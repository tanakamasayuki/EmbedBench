// Command-style AT modem model implementation. Pure C++11.
#include "modem_model.h"

#include <stdio.h>
#include <string.h>

void AtModemModel::reset() {
  hasPending_ = false;
  replyDueUs_ = 0;
  pending_[0] = '\0';
  replies_ = 0;
}

void AtModemModel::send(const char* text) {
  if (port() != nullptr) {
    port()->serialOut(reinterpret_cast<const uint8_t*>(text), strlen(text));
  }
  ++replies_;
}

void AtModemModel::serialIn(const uint8_t* data, size_t len) {
  if (len == 2 && data[0] == 'A' && data[1] == 'T') {
    // Plain "AT" answers on the wire immediately.
    send("OK");
  } else if (len > 2 && data[0] == 'A' && data[1] == 'T') {
    // Extended commands answer after a fixed processing latency, driven
    // entirely by advanceTo() so the behavior stays deterministic.
    snprintf(pending_, sizeof(pending_), "OK");
    replyDueUs_ = (port() != nullptr ? port()->nowMicros() : 0) + 1000;
    hasPending_ = true;
  }
}

void AtModemModel::advanceTo(uint64_t nowUs) {
  if (hasPending_ && nowUs >= replyDueUs_) {
    hasPending_ = false;
    send(pending_);
  }
}

size_t AtModemModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "modem replies=%u pending=%u", replies_,
                         hasPending_ ? 1 : 0);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
