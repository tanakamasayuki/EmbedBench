// Environmental sensor in the style of a BME280: a register map read over
// I2C with a repeated start, a forced measurement that takes 7.5 ms (not a
// multiple of a typical tick), a data-ready line, and a status register.
// Written against the frozen interface only, so it runs in any
// environment. Pure C++11.
#pragma once

#include <embedbench_device.h>

class EnvSensorModel : public ebdev::Device {
 public:
  // Registers, chosen to look like the real part.
  static const uint8_t kRegChipId = 0xD0;   // reads kChipId
  static const uint8_t kRegStatus = 0xF3;   // bit 3 = measuring
  static const uint8_t kRegCtrl = 0xF4;     // write 0x25 = forced measurement
  static const uint8_t kRegTemp = 0xFA;     // three bytes, big endian
  static const uint8_t kChipId = 0x60;
  static const uint8_t kCmdForced = 0x25;
  static const uint8_t kStatusMeasuring = 0x08;

  static const uint8_t kLineDataReady = 0;  // output line
  static const uint8_t kChannelTemp = 0;    // the world sets the temperature
  // A BME280 at standard oversampling takes about 8 ms; 7,500 is that,
  // and deliberately not a multiple of any tick this project uses.
  // Physical.
  static const uint64_t kMeasureUs = 7500;

  void reset() override;

  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

  uint32_t measurements() const { return measurements_; }

 private:
  uint8_t pointer_ = 0;
  uint8_t ctrl_ = 0;
  bool measuring_ = false;
  uint64_t readyAtUs_ = 0;
  int32_t rawTemp_ = 0;     // what the world is showing the sensor
  int32_t latched_ = 0;     // what a completed measurement captured
  uint32_t measurements_ = 0;
  uint32_t badRegisters_ = 0;
};
