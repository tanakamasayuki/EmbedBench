// IR unit implementation. Pure C++11.
#include "unit_ir_model.h"

#include <stdio.h>

void UnitIrModel::reset() {
  codeFormat_ = 0;
  repeatFormat_ = 0;
  resolved_ = false;
  address_ = 0;
  command_ = 0;
  repeatsLeft_ = 0;
  nextDueUs_ = 0;
  holding_ = false;
  rxAddress_ = 0;
  rxCommand_ = 0;
  sent_ = 0;
  received_ = 0;
}

void UnitIrModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  codeFormat_ = port()->formatId("m5.ir.nec.1",
                                 ebdev::schemaFingerprint("u8 addr,u8 cmd"));
  repeatFormat_ = port()->formatId("m5.ir.rep.1",
                                   ebdev::schemaFingerprint("empty"));
  resolved_ = true;
}

void UnitIrModel::frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
                          size_t bits) {
  // IR is a broadcast: there is no address filter on the link itself, so
  // anything of a known format on this bus is seen. A remote's own code
  // coming back is normal (an IR unit hears what it transmits).
  resolve();
  if (bus != kBus) return;
  if (format == repeatFormat_ && bits == 0) {
    ++received_;
    return;
  }
  if (format != codeFormat_ || bits != 16) return;
  rxAddress_ = data[0];
  rxCommand_ = data[1];
  ++received_;
}

void UnitIrModel::emit(bool repeat) {
  resolve();
  if (port() == nullptr) return;
  if (repeat) {
    // The NEC repeat code says only "still held" — no payload at all.
    if (repeatFormat_ != 0) port()->frameOut(kBus, repeatFormat_, nullptr, 0);
  } else {
    const uint8_t frame[2] = {address_, command_};
    if (codeFormat_ != 0) port()->frameOut(kBus, codeFormat_, frame, 16);
  }
  ++sent_;
}

void UnitIrModel::advanceTo(uint64_t nowUs) {
  while (holding_ && nowUs >= nextDueUs_) {
    emit(true);
    if (repeatsLeft_ == 0) {
      holding_ = false;
      break;
    }
    --repeatsLeft_;
    nextDueUs_ += kRepeatPeriodUs;
    if (port() != nullptr) port()->requestWake(nextDueUs_);
  }
}

bool UnitIrModel::channelWrite(uint8_t channel, const uint8_t* data,
                               size_t len) {
  if (channel != kChannelPress || len < 3) return false;
  address_ = data[0];
  command_ = data[1];
  emit(false);
  repeatsLeft_ = data[2];
  if (repeatsLeft_ == 0) return true;
  --repeatsLeft_;
  holding_ = true;
  nextDueUs_ = (port() != nullptr ? port()->nowMicros() : 0) + kRepeatPeriodUs;
  if (port() != nullptr) port()->requestWake(nextDueUs_);
  return true;
}

size_t UnitIrModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel != kChannelLastRx || cap < 2) return 0;
  out[0] = rxAddress_;
  out[1] = rxCommand_;
  return 2;
}

size_t UnitIrModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "ir sent=%u rx=%u last=%02X%02X held=%u",
                         sent_, received_, rxAddress_, rxCommand_,
                         holding_ ? 1u : 0u);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
