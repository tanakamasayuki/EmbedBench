// A relay unit in the style of M5Stack's Unit RELAY: the application
// drives a control line and the contact follows it. A real relay does not
// like being switched faster than its contacts can settle, so switching
// inside the minimum interval is reported through the diagnostic path —
// there is no return value on a line to refuse with. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitRelayModel : public ebdev::Device {
 public:
  static const uint8_t kLineControl = 0;    // input: driven by the sketch
  static const uint8_t kChannelState = 0;   // world: reads the contact
  static const uint64_t kSettleUs = 5000;   // minimum interval between flips

  void reset() override;
  void lineIn(uint8_t line, uint8_t level) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  bool closed_ = false;
  bool everSwitched_ = false;
  uint64_t lastSwitchUs_ = 0;
  uint32_t switches_ = 0;
  uint32_t chatter_ = 0;
};
