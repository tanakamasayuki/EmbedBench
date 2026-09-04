// Register-map device that REQUIRES a repeated start for reads: a read
// issued under a repeated start (pointer write without STOP, then read)
// returns register data; a standalone read returns nothing. Common for
// sensors and EEPROMs, and impossible to model without transaction
// context. Pure C++11.
#pragma once

#include <embedbench_device.h>

class RegisterMapModel : public ebdev::Device {
 public:
  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint8_t regs_[4] = {0, 0, 0, 0};
  uint8_t pointer_ = 0;
  uint32_t repeatedStartReads_ = 0;
  uint32_t plainReads_ = 0;
};
