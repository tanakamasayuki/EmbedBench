// Command-style AT modem model: a byte-stream parser (commands end with
// ';', call boundaries carry no meaning) with reply latency driven purely
// by advanceTo(). Pure C++11, no platform includes.
#pragma once

#include <embedbench_device.h>

class AtModemModel : public ebdev::Device {
 public:
  static const char kTerminator = ';';

  void reset() override;

  void serialIn(const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void dispatch();
  void send(const char* text);

  char line_[16] = {0};
  size_t lineLength_ = 0;
  uint32_t overflows_ = 0;
  uint32_t unknown_ = 0;
  bool hasPending_ = false;
  uint64_t replyDueUs_ = 0;
  char pending_[8] = {0};
  uint32_t replies_ = 0;
};
