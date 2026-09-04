// Command-style AT modem model: a state machine with reply latency driven
// purely by advanceTo(). Pure C++11, no platform includes.
#pragma once

#include <embedbench_device.h>

class AtModemModel : public ebdev::Device {
 public:
  void reset() override;

  void serialIn(const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void send(const char* text);

  bool hasPending_ = false;
  uint64_t replyDueUs_ = 0;
  char pending_[8] = {0};
  uint32_t replies_ = 0;
};
