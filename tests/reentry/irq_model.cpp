// IRQ-raising sensor model implementation. Pure C++11.
#include "irq_model.h"

#include <stdio.h>

void IrqSensorModel::reset() {
  pointer_ = 0;
  reads_ = 0;
  depth_ = 0;
  maxDepth_ = 0;
}

uint8_t IrqSensorModel::i2cWrite(const uint8_t* data, size_t len,
                                 const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 1) pointer_ = data[0];
  return ebdev::kI2cAck;
}

size_t IrqSensorModel::i2cRead(uint8_t* data, size_t len,
                               const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  ++depth_;
  if (depth_ > maxDepth_) maxDepth_ = depth_;
  // The first read raises the IRQ line while the read is still in flight.
  // The counter moves first so that an environment which (against the
  // contract) re-enters this method does not raise the line again forever.
  const bool first = reads_ == 0;
  ++reads_;
  if (first && port() != nullptr) port()->lineOut(kLineIrq, 1);
  size_t count = 0;
  if (len >= 2) {
    data[0] = 0x01;
    data[1] = 0x02;
    count = 2;
  }
  --depth_;
  return count;
}

size_t IrqSensorModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "irq reads=%u max_depth=%u", reads_, maxDepth_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
