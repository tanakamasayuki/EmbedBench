// Remote-node model implementation. Pure C++11.
#include "node_model.h"

#include <stdio.h>

void RemoteNodeModel::reset() {
  commandFormat_ = 0;
  telemetryFormat_ = 0;
  resolved_ = false;
  power_ = 0;
  hasPending_ = false;
  replyDueUs_ = 0;
}

void RemoteNodeModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  commandFormat_ = port()->formatId("acme.node.1", ebdev::schemaFingerprint("u8 addr,u8 cmd"));
  telemetryFormat_ = port()->formatId("acme.tele.1", ebdev::schemaFingerprint("u8 addr,u8 power"));
  resolved_ = true;
}

void RemoteNodeModel::frameIn(uint8_t bus, uint16_t format,
                              const uint8_t* data, size_t bits) {
  // Interpreting the bits is this device's business, keyed by the format
  // id; frames on other links, of other formats, or for other addresses
  // are silently ignored, the way an addressed radio ignores foreign
  // traffic.
  resolve();
  if (commandFormat_ == 0 || bus != kBus || format != commandFormat_ || bits != 16) return;
  if (data[0] != kAddress) return;
  power_ = data[1] == kCommandPowerOn ? 1 : 0;
  replyDueUs_ = (port() != nullptr ? port()->nowMicros() : 0) + kReplyLatencyUs;
  hasPending_ = true;
}

void RemoteNodeModel::advanceTo(uint64_t nowUs) {
  if (hasPending_ && nowUs >= replyDueUs_) {
    hasPending_ = false;
    resolve();
    const uint8_t frame[2] = {kAddress, power_};
    if (telemetryFormat_ != 0 && port() != nullptr) {
      port()->frameOut(kBus, telemetryFormat_, frame, 16);
    }
  }
}

size_t RemoteNodeModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "node power=%u pending=%u", power_,
                         hasPending_ ? 1 : 0);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
