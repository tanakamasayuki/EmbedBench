// Remote-node model implementation. Pure C++11.
#include "node_model.h"

#include <stdio.h>

void RemoteNodeModel::reset() {
  power_ = 0;
  hasPending_ = false;
  replyDueUs_ = 0;
}

void RemoteNodeModel::frameIn(uint8_t bus, uint16_t format,
                              const uint8_t* data, size_t bits) {
  // Interpreting the bits is this device's business, keyed by the format
  // id; frames on other links, of other formats, or for other addresses
  // are silently ignored, the way an addressed radio ignores foreign
  // traffic.
  if (bus != kBus || format != kFormatCommand || bits != 16) return;
  if (data[0] != kAddress) return;
  power_ = data[1] == kCommandPowerOn ? 1 : 0;
  replyDueUs_ = (port() != nullptr ? port()->nowMicros() : 0) + kReplyLatencyUs;
  hasPending_ = true;
}

void RemoteNodeModel::advanceTo(uint64_t nowUs) {
  if (hasPending_ && nowUs >= replyDueUs_) {
    hasPending_ = false;
    const uint8_t frame[2] = {kAddress, power_};
    if (port() != nullptr) port()->frameOut(kBus, kFormatTelemetry, frame, 16);
  }
}

size_t RemoteNodeModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "node power=%u pending=%u", power_,
                         hasPending_ ? 1 : 0);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
