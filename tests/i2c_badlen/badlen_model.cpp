// Misbehaving register-map model implementation. Pure C++11.
#include "badlen_model.h"

#include <stdio.h>

void BadLengthModel::reset() { reads_ = 0; }

uint8_t BadLengthModel::i2cWrite(const uint8_t*, size_t,
                                 const ebdev::I2cTransfer&) {
  return ebdev::kI2cAck;
}

size_t BadLengthModel::i2cRead(uint8_t* data, size_t len,
                               const ebdev::I2cTransfer&) {
  ++reads_;
  for (size_t i = 0; i < len; ++i) data[i] = static_cast<uint8_t>(0xA0 + i);
  return len + 1;  // contract violation: more than the buffer holds
}

size_t BadLengthModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "badlen reads=%u", reads_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
