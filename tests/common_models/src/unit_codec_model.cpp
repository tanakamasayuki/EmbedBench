// Audio codec implementation. Pure C++11.
#include "unit_codec_model.h"

#include <stdio.h>

void UnitCodecModel::reset() {
  reg_ = 0;
  volume_ = 0xFF;
  muted_ = false;
  selected_ = false;
  samples_ = 0;
  unselected_ = 0;
  last_ = 0;
}

uint8_t UnitCodecModel::i2cWrite(const uint8_t* data, size_t len,
                                 const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 0) return ebdev::kI2cAddressNack;
  reg_ = data[0];
  if (reg_ == kRegVolume && len >= 2) {
    volume_ = data[1];
    return ebdev::kI2cAck;
  }
  if (reg_ == kRegMute && len >= 2) {
    muted_ = data[1] != 0;
    return ebdev::kI2cAck;
  }
  if (reg_ != kRegVolume && reg_ != kRegMute && reg_ != kRegCount) {
    if (port() != nullptr) port()->diagnose("write to unmapped register");
    return ebdev::kI2cDataNack;
  }
  return ebdev::kI2cAck;
}

size_t UnitCodecModel::i2cRead(uint8_t* data, size_t len,
                               const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 0) return 0;
  if (reg_ == kRegCount) {
    data[0] = static_cast<uint8_t>(samples_ & 0xFF);
    return 1;
  }
  if (reg_ == kRegVolume) {
    data[0] = volume_;
    return 1;
  }
  if (reg_ == kRegMute) {
    data[0] = muted_ ? 1 : 0;
    return 1;
  }
  return 0;
}

void UnitCodecModel::lineIn(uint8_t line, uint8_t level) {
  if (line != kLineSelect) return;
  selected_ = level == 0;
}

uint8_t UnitCodecModel::spiTransfer(uint8_t mosi) {
  if (!selected_) {
    // Data arriving with the part unselected is a wiring or ordering
    // mistake, and one a bench would never show.
    ++unselected_;
    if (port() != nullptr) port()->diagnose("sample while unselected");
    return 0;
  }
  ++samples_;
  // What the control bus was told decides what the data bus does.
  if (muted_) {
    last_ = 0;
  } else {
    last_ = static_cast<uint8_t>((static_cast<uint16_t>(mosi) * volume_) / 255);
  }
  return last_;
}

size_t UnitCodecModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "codec vol=%u mute=%u n=%u last=%02X stray=%u",
                         volume_, muted_ ? 1u : 0u, samples_, last_,
                         unselected_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
