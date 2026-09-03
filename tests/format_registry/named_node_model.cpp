// Named-format node model implementation. Pure C++11.
#include "named_node_model.h"

#include <stdio.h>

void NamedNodeModel::reset() {
  commandId_ = 0;
  telemetryId_ = 0;
  resolved_ = false;
  power_ = 0;
  hasPending_ = false;
  replyDueUs_ = 0;
}

void NamedNodeModel::resolveFormats() {
  if (resolved_ || port() == nullptr) return;
  // Names are the cross-library identity; the numeric ids are local to
  // this environment. Resolved once, then every frame compares integers.
  commandId_ = port()->formatId("node.cmd");
  telemetryId_ = port()->formatId("node.tel");
  resolved_ = true;
}

void NamedNodeModel::frameIn(uint8_t bus, uint16_t format,
                             const uint8_t* data, size_t bits) {
  resolveFormats();
  // Id 0 matches nothing, so an environment without a registry leaves
  // this device safely inert instead of mis-matching.
  if (commandId_ == 0 || bus != kBus || format != commandId_ || bits != 16) {
    return;
  }
  if (data[0] != kAddress) return;
  power_ = data[1] == kCommandPowerOn ? 1 : 0;
  replyDueUs_ = port()->nowMicros() + kReplyLatencyUs;
  hasPending_ = true;
}

void NamedNodeModel::advanceTo(uint64_t nowUs) {
  if (hasPending_ && nowUs >= replyDueUs_) {
    hasPending_ = false;
    resolveFormats();
    const uint8_t frame[2] = {kAddress, power_};
    if (telemetryId_ != 0 && port() != nullptr) {
      port()->frameOut(kBus, telemetryId_, frame, 16);
    }
  }
}

size_t NamedNodeModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "node power=%u pending=%u", power_,
                         hasPending_ ? 1 : 0);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
