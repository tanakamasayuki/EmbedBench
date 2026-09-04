// Command-style AT modem model implementation. Pure C++11.
#include "modem_model.h"

#include <stdio.h>
#include <string.h>

void AtModemModel::reset() {
  lineLength_ = 0;
  overflows_ = 0;
  unknown_ = 0;
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

// Byte stream: bytes are accumulated across calls until the terminator,
// so "AT+S;" in one call and five single-byte calls are the same input.
void AtModemModel::serialIn(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = static_cast<char>(data[i]);
    if (c == kTerminator) {
      dispatch();
    } else if (lineLength_ < sizeof(line_)) {
      line_[lineLength_++] = c;
    } else {
      ++overflows_;  // command longer than the line buffer: discarded
      lineLength_ = 0;
    }
  }
}

void AtModemModel::dispatch() {
  if (lineLength_ == 2 && line_[0] == 'A' && line_[1] == 'T') {
    // Plain "AT" answers on the wire immediately.
    send("OK");
  } else if (lineLength_ > 2 && line_[0] == 'A' && line_[1] == 'T') {
    // Extended commands answer after a fixed processing latency, driven
    // entirely by advanceTo() so the behavior stays deterministic.
    snprintf(pending_, sizeof(pending_), "OK");
    replyDueUs_ = (port() != nullptr ? port()->nowMicros() : 0) + 1000;
    hasPending_ = true;
  } else {
    ++unknown_;
  }
  lineLength_ = 0;
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
