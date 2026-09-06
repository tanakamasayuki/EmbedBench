// A potentiometer unit in the style of M5Stack's Unit ANGLE: the knob
// position is a voltage, nothing more. The unit presents both the raw
// count an ADC would report and the millivolts, because an environment
// cannot derive one from the other. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitAngleModel : public ebdev::Device {
 public:
  static const uint8_t kLineAnalog = 0;      // output: the wiper voltage
  static const uint8_t kChannelAngle = 0;    // world: 0..kFullScale
  static const uint16_t kFullScale = 4095;   // 12-bit, like the ADC reading
  static const uint32_t kFullScaleMv = 3300;

  void reset() override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void present();

  uint16_t position_ = 0;
  uint32_t updates_ = 0;
};
