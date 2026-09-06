// Encoder unit model implementation. Pure C++11.
#include "unit_encoder_model.h"

#include <stdio.h>

void UnitEncoderModel::reset() {
  pointer_ = 0;
  counter_ = 0;
  pressed_ = false;
  led_[0] = led_[1] = led_[2] = 0;
  ledWrites_ = 0;
}

uint8_t UnitEncoderModel::i2cWrite(const uint8_t* data, size_t len,
                                   const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 0) return ebdev::kI2cDataNack;
  pointer_ = data[0];
  if (len == 1) return ebdev::kI2cAck;
  if (pointer_ == kRegCounter && len == 3) {
    // Writing the counter is how a sketch zeroes the knob.
    counter_ = static_cast<int16_t>(static_cast<uint16_t>(data[1]) |
                                    (static_cast<uint16_t>(data[2]) << 8));
    return ebdev::kI2cAck;
  }
  if (pointer_ == kRegLed && len == 4) {
    led_[0] = data[1];
    led_[1] = data[2];
    led_[2] = data[3];
    ++ledWrites_;
    return ebdev::kI2cAck;
  }
  if (port() != nullptr) port()->diagnose("write to a read-only register");
  return ebdev::kI2cDataNack;
}

size_t UnitEncoderModel::i2cRead(uint8_t* data, size_t len,
                                 const ebdev::I2cTransfer& xfer) {
  // This part keeps its pointer between transactions, so a plain read
  // works as well as one under a repeated start.
  (void)xfer;
  if (pointer_ == kRegCounter && len >= 2) {
    const uint16_t raw = static_cast<uint16_t>(counter_);
    data[0] = static_cast<uint8_t>(raw & 0xFF);
    data[1] = static_cast<uint8_t>(raw >> 8);
    return 2;
  }
  if (pointer_ == kRegButton && len >= 1) {
    data[0] = pressed_ ? 1 : 0;
    return 1;
  }
  return 0;
}

bool UnitEncoderModel::channelWrite(uint8_t channel, const uint8_t* data,
                                    size_t len) {
  if (channel == kChannelTurn && len == 1) {
    // One byte of signed delta: the knob moved that many detents.
    counter_ = static_cast<int16_t>(counter_ +
                                    static_cast<int8_t>(data[0]));
    return true;
  }
  if (channel == kChannelButton && len == 1) {
    pressed_ = data[0] != 0;
    return true;
  }
  return false;
}

size_t UnitEncoderModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "encoder count=%d pressed=%u led=%02X%02X%02X w=%u",
                         counter_, pressed_ ? 1 : 0, led_[0], led_[1], led_[2],
                         ledWrites_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
