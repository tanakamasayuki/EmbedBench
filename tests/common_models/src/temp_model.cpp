// Register-map temperature sensor model implementation. Pure C++11.
#include "temp_model.h"

#include <stdio.h>

void TempSensorModel::reset() {
  config_ = 0;
  tempRaw_ = 0;
  pointer_ = 0;
}

uint8_t TempSensorModel::i2cWrite(const uint8_t* data, size_t len,
                                  const ebdev::I2cTransfer& xfer) {
  // This sensor accepts reads with or without a repeated start, so the
  // transaction context does not change its behavior.
  (void)xfer;
  if (len == 1) {
    pointer_ = data[0];
  } else if (len == 2 && data[0] == 0x01) {
    config_ = data[1];
  }
  return ebdev::kI2cAck;
}

size_t TempSensorModel::i2cRead(uint8_t* data, size_t len,
                                const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (pointer_ == 0x00 && len >= 2) {
    data[0] = static_cast<uint8_t>(tempRaw_ >> 8);
    data[1] = static_cast<uint8_t>(tempRaw_ & 0xFF);
    return 2;
  }
  return 0;
}

bool TempSensorModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  if (channel != kChannelTemp || len != 2) return false;
  tempRaw_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
  // New sample: raise the data-ready line toward the world.
  if (port() != nullptr) port()->lineOut(kLineDataReady, 1);
  return true;
}

size_t TempSensorModel::channelRead(uint8_t channel, uint8_t* out,
                                    size_t cap) {
  if (channel != kChannelTemp) return 0;
  if (cap >= 1) out[0] = static_cast<uint8_t>(tempRaw_ >> 8);
  if (cap >= 2) out[1] = static_cast<uint8_t>(tempRaw_ & 0xFF);
  return 2;  // needed length, regardless of how much fit
}

size_t TempSensorModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "temp=%04X cfg=%02X", tempRaw_, config_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
