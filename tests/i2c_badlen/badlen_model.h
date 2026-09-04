// A deliberately misbehaving device: its i2cRead() claims one byte more
// than the buffer holds. The interface says environments must diagnose
// this rather than trust the count, which would read past the buffer.
// Pure C++11; local to this experiment because it violates the contract
// on purpose.
#pragma once

#include <embedbench_device.h>

class BadLengthModel : public ebdev::Device {
 public:
  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint32_t reads_ = 0;
};
