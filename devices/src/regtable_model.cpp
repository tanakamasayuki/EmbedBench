// Table-driven register-map model implementation. Pure C++11.
#include "regtable_model.h"

#include <stdio.h>

RegTableModel::RegTableModel(const RegTableSpec& spec) : spec_(&spec) {
  size_t used = 0;
  for (size_t i = 0; i < spec.entryCount && count_ < kMaxRegisters; ++i) {
    const size_t w = spec.entries[i].width;
    if (w == 0 || used + w > kValueBytes) break;
    offset_[count_++] = static_cast<uint16_t>(used);
    used += w;
  }
  loadReset();
}

void RegTableModel::loadReset() {
  for (size_t i = 0; i < count_; ++i) {
    const RegTableEntry& e = spec_->entries[i];
    for (size_t k = 0; k < e.width; ++k) {
      values_[offset_[i] + k] = e.reset != nullptr ? e.reset[k] : 0;
    }
  }
}

void RegTableModel::reset() {
  loadReset();
  pointer_ = 0;
  reads_ = 0;
  writes_ = 0;
  unmapped_ = 0;
  refused_ = 0;
}

int RegTableModel::find(uint8_t address) const {
  for (size_t i = 0; i < count_; ++i) {
    if (spec_->entries[i].address == address) return static_cast<int>(i);
  }
  return -1;
}

size_t RegTableModel::width(uint8_t address) const {
  const int idx = find(address);
  return idx < 0 ? 0 : spec_->entries[idx].width;
}

uint8_t RegTableModel::get(uint8_t address, size_t offset) const {
  const int idx = find(address);
  if (idx < 0 || offset >= spec_->entries[idx].width) return 0;
  return values_[offset_[idx] + offset];
}

void RegTableModel::set(uint8_t address, size_t offset, uint8_t value) {
  const int idx = find(address);
  if (idx < 0 || offset >= spec_->entries[idx].width) return;
  values_[offset_[idx] + offset] = value;
}

void RegTableModel::setBytes(uint8_t address, const uint8_t* value,
                             size_t len) {
  for (size_t k = 0; k < len; ++k) set(address, k, value[k]);
}

uint8_t RegTableModel::i2cWrite(const uint8_t* data, size_t len,
                                const ebdev::I2cTransfer& xfer) {
  (void)xfer;  // a pointer write may or may not be followed by STOP
  if (len == 0) return ebdev::kI2cAck;  // address probe: the part is there
  pointer_ = data[0];
  if (len == 1) return ebdev::kI2cAck;
  const int idx = find(pointer_);
  char text[64];
  if (idx < 0 || (spec_->entries[idx].flags & kWritable) == 0) {
    ++refused_;
    snprintf(text, sizeof(text), "write refused: register %02X not writable",
             pointer_);
    if (port() != nullptr) port()->diagnose(text);
    return ebdev::kI2cDataNack;
  }
  const RegTableEntry& e = spec_->entries[idx];
  if (len - 1 > e.width) {
    ++refused_;
    snprintf(text, sizeof(text),
             "write refused: %u bytes into %u-byte register %02X",
             static_cast<unsigned>(len - 1), e.width, pointer_);
    if (port() != nullptr) port()->diagnose(text);
    return ebdev::kI2cDataNack;
  }
  for (size_t k = 0; k + 1 < len; ++k) values_[offset_[idx] + k] = data[k + 1];
  ++writes_;
  afterWrite(pointer_, data + 1, len - 1);
  return ebdev::kI2cAck;
}

size_t RegTableModel::i2cRead(uint8_t* data, size_t len,
                              const ebdev::I2cTransfer& xfer) {
  if (spec_->requireRepeatedStart && !xfer.continued) return 0;
  const size_t hooked = onRead(pointer_, data, len);
  if (hooked != kPassThrough) return hooked <= len ? hooked : 0;
  const int idx = find(pointer_);
  if (idx < 0) {
    ++unmapped_;
    char text[48];
    snprintf(text, sizeof(text), "read of unmapped register %02X", pointer_);
    if (port() != nullptr) port()->diagnose(text);
    return 0;
  }
  const RegTableEntry& e = spec_->entries[idx];
  const size_t count = len < e.width ? len : e.width;
  for (size_t k = 0; k < count; ++k) data[k] = values_[offset_[idx] + k];
  ++reads_;
  return count;
}

bool RegTableModel::channelWrite(uint8_t channel, const uint8_t* data,
                                 size_t len) {
  for (size_t i = 0; i < spec_->channelCount; ++i) {
    if (spec_->channels[i].channel != channel) continue;
    const int idx = find(spec_->channels[i].address);
    if (idx < 0 || len != spec_->entries[idx].width) return false;
    for (size_t k = 0; k < len; ++k) values_[offset_[idx] + k] = data[k];
    return true;
  }
  return false;
}

size_t RegTableModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  for (size_t i = 0; i < spec_->channelCount; ++i) {
    if (spec_->channels[i].channel != channel) continue;
    const int idx = find(spec_->channels[i].address);
    if (idx < 0) return ebdev::kChannelUnsupported;
    const RegTableEntry& e = spec_->entries[idx];
    for (size_t k = 0; k < e.width && k < cap; ++k) {
      out[k] = values_[offset_[idx] + k];
    }
    return e.width;
  }
  return ebdev::kChannelUnsupported;
}

size_t RegTableModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "regtable ptr=%02X regs=%u rd=%u wr=%u unmapped=%u "
                         "refused=%u",
                         pointer_, static_cast<unsigned>(count_), reads_,
                         writes_, unmapped_, refused_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
