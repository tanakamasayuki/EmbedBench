// Repeated-start register-map model implementation. Pure C++11.
#include "regmap_model.h"

#include <stdio.h>

void RegisterMapModel::reset() {
  regs_[0] = 0x00;
  regs_[1] = 0x5A;  // power-on value of register 1
  regs_[2] = 0x00;
  regs_[3] = 0x00;
  pointer_ = 0;
  repeatedStartReads_ = 0;
  plainReads_ = 0;
}

uint8_t RegisterMapModel::i2cWrite(const uint8_t* data, size_t len,
                                   const ebdev::I2cTransfer& xfer) {
  (void)xfer;  // a pointer write may or may not be followed by STOP
  if (len == 1) {
    if (data[0] >= sizeof(regs_)) return ebdev::kI2cDataNack;
    pointer_ = data[0];
    return ebdev::kI2cAck;
  }
  if (len == 2 && data[0] < sizeof(regs_)) {
    regs_[data[0]] = data[1];
    pointer_ = data[0];
    return ebdev::kI2cAck;
  }
  return ebdev::kI2cDataNack;
}

size_t RegisterMapModel::i2cRead(uint8_t* data, size_t len,
                                 const ebdev::I2cTransfer& xfer) {
  if (!xfer.continued) {
    // No repeated start: this device does not answer a standalone read.
    ++plainReads_;
    return 0;
  }
  ++repeatedStartReads_;
  size_t count = 0;
  while (count < len && pointer_ + count < sizeof(regs_)) {
    data[count] = regs_[pointer_ + count];
    ++count;
  }
  return count;
}

size_t RegisterMapModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "regmap ptr=%u r1=%02X rs=%u plain=%u",
                         pointer_, regs_[1], repeatedStartReads_, plainReads_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
