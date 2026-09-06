// UWB anchor implementation. Pure C++11.
#include "unit_uwb_model.h"

#include <stdio.h>

void UnitUwbModel::reset() {
  pollFormat_ = 0;
  respFormat_ = 0;
  resolved_ = false;
  distanceMm_ = 0;
  pending_ = false;
  replyDueUs_ = 0;
  tag_ = 0;
  seq_ = 0;
  answered_ = 0;
  collided_ = 0;
}

void UnitUwbModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  pollFormat_ = port()->formatId("m5.uwb.poll.1",
                                 ebdev::schemaFingerprint("u4 tag,u8 seq"));
  respFormat_ = port()->formatId("m5.uwb.resp.1",
                                 ebdev::schemaFingerprint("u8 anchor,u16 mm"));
  resolved_ = true;
}

void UnitUwbModel::frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
                           size_t bits) {
  resolve();
  if (bus != kBus || format != pollFormat_ || bits != 12) return;
  if (pending_) {
    // A tag that polls again before the round finishes would put two
    // answers in one slot; a real anchor ignores the second poll.
    ++collided_;
    if (port() != nullptr) port()->diagnose("poll during ranging");
    return;
  }
  // 12 bits, MSB-first: tag is bits 0-3, sequence is bits 4-11.
  tag_ = static_cast<uint8_t>(data[0] >> 4);
  seq_ = static_cast<uint8_t>((data[0] << 4) | (data[1] >> 4));
  pending_ = true;
  replyDueUs_ = (port() != nullptr ? port()->nowMicros() : 0) +
                kBaseLatencyUs + anchorId_ * kSlotUs;
  if (port() != nullptr) port()->requestWake(replyDueUs_);
}

void UnitUwbModel::advanceTo(uint64_t nowUs) {
  if (!pending_ || nowUs < replyDueUs_) return;
  pending_ = false;
  resolve();
  const uint8_t frame[3] = {anchorId_,
                            static_cast<uint8_t>(distanceMm_ >> 8),
                            static_cast<uint8_t>(distanceMm_ & 0xFF)};
  if (respFormat_ != 0 && port() != nullptr) {
    port()->frameOut(kBus, respFormat_, frame, 24);
  }
  ++answered_;
}

bool UnitUwbModel::channelWrite(uint8_t channel, const uint8_t* data,
                                size_t len) {
  if (channel != kChannelDistance || len < 2) return false;
  distanceMm_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
  return true;
}

size_t UnitUwbModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "uwb id=%u mm=%u tag=%u seq=%u answered=%u coll=%u",
                         anchorId_, distanceMm_, tag_, seq_, answered_,
                         collided_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
