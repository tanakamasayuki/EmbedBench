// An audio codec: the first model that lives on two buses at once. Its
// configuration arrives over I2C and its samples over SPI, and the two
// are one device — what the control bus was told changes what the data
// bus does, which is why they cannot be modelled as separate parts.
//
// The interface already allows this: i2cWrite/i2cRead and spiTransfer
// are methods on the same Device, so a model overrides both and the
// environment binds it twice. Whether that actually holds together is
// what this model is here to find out. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitCodecModel : public ebdev::Device {
 public:
  static const uint8_t kRegVolume = 0x00;  // 0-255, scales every sample
  static const uint8_t kRegMute = 0x01;    // non-zero silences the data bus
  static const uint8_t kRegCount = 0x02;   // read: samples seen, low byte
  static const uint8_t kLineSelect = 0;    // active low, frames a block

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  void lineIn(uint8_t line, uint8_t level) override;
  uint8_t spiTransfer(uint8_t mosi) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint8_t reg_ = 0;
  uint8_t volume_ = 0xFF;
  bool muted_ = false;
  bool selected_ = false;
  uint32_t samples_ = 0;
  uint32_t unselected_ = 0;
  uint8_t last_ = 0;
};
