// A rotary encoder in the style of M5Stack's Unit ENCODER: an I2C register
// map holding a signed counter the world turns, a button, and an RGB LED
// the application writes. Unlike the environmental sensor it answers a
// plain read as well as one under a repeated start, which is the other
// half of what real parts do. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitEncoderModel : public ebdev::Device {
 public:
  static const uint8_t kRegCounter = 0x00;  // int16, little endian
  static const uint8_t kRegButton = 0x10;   // 1 while pressed
  static const uint8_t kRegLed = 0x20;      // write three bytes: R, G, B
  static const uint8_t kChannelTurn = 0;    // world: signed delta
  static const uint8_t kChannelButton = 1;  // world: 1 = pressed

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint8_t pointer_ = 0;
  int16_t counter_ = 0;
  bool pressed_ = false;
  uint8_t led_[3] = {0, 0, 0};
  uint32_t ledWrites_ = 0;
};
