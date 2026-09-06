// A button unit in the style of M5Stack's Unit BUTTON: one signal line,
// active low, driven by whoever is pressing it in the world. The simplest
// shape a device can have — no bus at all, just a line and the world.
// Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitButtonModel : public ebdev::Device {
 public:
  static const uint8_t kLineSignal = 0;   // output: low while pressed
  static const uint8_t kChannelPress = 0; // world: 1 = pressed

  void reset() override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  bool pressed_ = false;
  uint32_t presses_ = 0;
};
