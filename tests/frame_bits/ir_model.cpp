// IR receiver model implementation. Pure C++11.
#include "ir_model.h"

#include <stdio.h>

void IrReceiverModel::reset() {
  commandFormat_ = 0;
  statusFormat_ = 0;
  resolved_ = false;
  lastValue_ = 0;
  frames_ = 0;
  triggers_ = 0;
  unsent_ = 0;
}

void IrReceiverModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  commandFormat_ = port()->formatId("acme.ir.1", 0x000C);
  statusFormat_ = port()->formatId("acme.irstat.1", 0x0080);
  resolved_ = true;
}

void IrReceiverModel::frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
                              size_t bits) {
  resolve();
  if (commandFormat_ == 0 || bus != kBus || format != commandFormat_) return;
  if (bits == 0) {
    ++triggers_;  // an empty frame is a valid trigger
    return;
  }
  if (bits != 12) return;
  // MSB-first packing: frame bit 0 is bit 7 of data[0]; the 12-bit value
  // occupies data[0] and the high nibble of data[1].
  lastValue_ = static_cast<uint16_t>((data[0] << 4) | (data[1] >> 4));
  ++frames_;
}

bool IrReceiverModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  (void)data;
  (void)len;
  if (channel != kChannelStatus) return false;
  status();
  return true;
}

// One atomic 128-bit frame: sent whole or counted as unsent, never split.
void IrReceiverModel::status() {
  resolve();
  if (port() == nullptr || statusFormat_ == 0) return;
  uint8_t frame[16] = {0};
  frame[0] = static_cast<uint8_t>(lastValue_ >> 8);
  frame[1] = static_cast<uint8_t>(lastValue_ & 0xFF);
  frame[2] = static_cast<uint8_t>(frames_);
  frame[3] = static_cast<uint8_t>(triggers_);
  if (!port()->frameOut(kBus, statusFormat_, frame, 128)) ++unsent_;
}

size_t IrReceiverModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "ir value=%03X frames=%u triggers=%u unsent=%u",
                         lastValue_, frames_, triggers_, unsent_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
