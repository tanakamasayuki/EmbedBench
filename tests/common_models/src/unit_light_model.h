// A light sensor in the style of M5Stack's Unit LIGHT: one analog output
// for the brightness and one digital output that trips at a threshold the
// unit's own potentiometer sets. Two outputs from one physical quantity,
// which is what makes it worth having next to the plain analog unit.
// Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitLightModel : public ebdev::Device {
 public:
  static const uint8_t kLineAnalog = 0;       // output: brightness
  static const uint8_t kLineDigital = 0;      // output: 1 while dark
  static const uint8_t kChannelBrightness = 0;
  static const uint8_t kChannelThreshold = 1;
  static const uint16_t kDefaultThreshold = 2000;

  void reset() override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void present();

  uint16_t brightness_ = 0;
  uint16_t threshold_ = kDefaultThreshold;
  bool dark_ = false;
  uint32_t trips_ = 0;
};
