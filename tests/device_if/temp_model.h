// Register-map temperature sensor model. Pure C++11: depends only on the
// portable device interface, compiles with no platform at all.
#pragma once

#include <embedbench_device.h>

class TempSensorModel : public ebdev::Device {
 public:
  // Logical output line 0 is the data-ready (DRDY) line.
  static const uint8_t kLineDataReady = 0;

  void reset() override;

  uint8_t i2cWrite(const uint8_t* data, size_t len) override;
  size_t i2cRead(uint8_t* data, size_t len) override;
  void channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint8_t config_ = 0;
  uint16_t tempRaw_ = 0;
  uint8_t pointer_ = 0;
};
