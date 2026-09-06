// Model exercising interface revisions 002-004. Pure C++11.
#include "rev1_model.h"

#include <stdio.h>

void Rev1Model::reset() {
  raw_ = 0;
  pending_ = false;
  dueUs_ = 0;
  wakeAccepted_ = false;
  analogPushes_ = 0;
  notes_ = 0;
}

bool Rev1Model::channelWrite(uint8_t channel, const uint8_t* data,
                             size_t len) {
  if (channel != kChannelTemp || len != 2) return false;
  raw_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
  // Revision 002: the device presents its own voltage, with no director
  // step between the model and what the application reads.
  if (port() != nullptr && port()->analogOut(kLineAnalog, raw_)) {
    ++analogPushes_;
  }
  return true;
}

void Rev1Model::serialIn(const uint8_t* data, size_t len) {
  if (port() == nullptr) return;
  for (size_t i = 0; i < len; ++i) {
    if (data[i] == '?') {
      // Revision 004: serial has no return value to refuse with, so the
      // device says so through the diagnostic path instead of silently.
      if (port()->diagnose("unknown command")) ++notes_;
      continue;
    }
    dueUs_ = port()->nowMicros() + kLatencyUs;
    pending_ = true;
    // Revision 003: ask to be advanced when the reply is actually due.
    wakeAccepted_ = port()->requestWake(dueUs_);
  }
}

void Rev1Model::advanceTo(uint64_t nowUs) {
  if (pending_ && nowUs >= dueUs_) {
    pending_ = false;
    const uint8_t reply[2] = {'O', 'K'};
    if (port() != nullptr) port()->serialOut(reply, sizeof(reply));
  }
}

size_t Rev1Model::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "rev1 raw=%u pushes=%u notes=%u wake=%u",
                         raw_, analogPushes_, notes_, wakeAccepted_ ? 1 : 0);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
