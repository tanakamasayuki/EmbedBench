// Sensor that raises its IRQ line from INSIDE an I2C read — the case that
// tempts an application ISR to access the same device again while the
// first read is still on the stack. Counts its own call depth so the
// environment's re-entrancy contract can be measured. Pure C++11.
#pragma once

#include <embedbench_device.h>

class IrqSensorModel : public ebdev::Device {
 public:
  static const uint8_t kLineIrq = 0;

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  size_t dump(char* out, size_t cap) override;

  uint32_t maxDepth() const { return maxDepth_; }
  uint32_t reads() const { return reads_; }

 private:
  uint8_t pointer_ = 0;
  uint32_t reads_ = 0;
  uint32_t depth_ = 0;
  uint32_t maxDepth_ = 0;
};
