// A motion sensor in the style of M5Stack's Unit PIR: motion raises the
// signal line, and the line stays up for a hold time of the sensor's own
// choosing after the motion stops. The hold is the device's behaviour, so
// it is driven by advanceTo() and asks the environment to wake it.
// Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitPirModel : public ebdev::Device {
 public:
  static const uint8_t kLineSignal = 0;    // output: high while triggered
  static const uint8_t kChannelMotion = 0; // world: 1 = motion seen
  // COMPRESSED. A real PIR unit holds for seconds to minutes; 2,500 us
  // keeps the behaviour (a hold that outlasts the motion) without a
  // test spending minutes of virtual time and a trace to match.
  static const uint64_t kHoldUs = 2500;

  void reset() override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  bool asserted_ = false;
  uint64_t releaseAtUs_ = 0;
  uint32_t triggers_ = 0;
};
