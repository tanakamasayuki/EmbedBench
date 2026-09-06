// An ultrasonic ranger in the style of M5Stack's Unit Ultrasonic I/O: the
// application pulses the trigger line, and the unit answers with an echo
// pulse whose WIDTH is the measurement. Distance is time here, so this is
// the model that shows whether an environment can place a device's edges
// where the device asked for them. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitSonicModel : public ebdev::Device {
 public:
  static const uint8_t kLineTrigger = 0;   // input: pulsed by the sketch
  static const uint8_t kLineEcho = 0;      // output: width carries the range
  static const uint8_t kChannelRange = 0;  // world: distance in millimetres
  // The part waits this long after the trigger before the echo starts,
  // and the echo lasts 5.8 us per millimetre (sound there and back).
  static const uint64_t kStartDelayUs = 450;
  static const uint64_t kUsPerMm = 6;  // rounded, so the arithmetic is exact

  void reset() override;
  void lineIn(uint8_t line, uint8_t level) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  enum class Phase : uint8_t { kIdle, kWaiting, kEchoing };

  uint16_t rangeMm_ = 0;
  Phase phase_ = Phase::kIdle;
  uint64_t dueUs_ = 0;
  uint32_t pulses_ = 0;
  uint32_t ignored_ = 0;
};
