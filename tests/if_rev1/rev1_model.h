// A device that needs all three of revisions 002-004 at once: it presents
// a voltage on an analog line, answers after a latency that does not
// divide by the environment's tick, and reports a protocol error it has
// no return value to express. Pure C++11.
#pragma once

#include <embedbench_device.h>

class Rev1Model : public ebdev::Device {
 public:
  static const uint8_t kChannelTemp = 0;
  static const uint8_t kLineAnalog = 0;
  static const uint64_t kLatencyUs = 1500;  // deliberately not a tick

  void reset() override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  void serialIn(const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint16_t raw_ = 0;
  bool pending_ = false;
  uint64_t dueUs_ = 0;
  bool wakeAccepted_ = false;
  uint32_t analogPushes_ = 0;
  uint32_t notes_ = 0;
};
