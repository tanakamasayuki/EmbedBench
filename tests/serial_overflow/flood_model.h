// A device whose reply is longer than a small receive queue, so the
// capacity-shortfall branch of HostPort::serialOut can be measured: the
// accepted prefix is delivered, the rest is dropped, a diagnostic is
// recorded, and the device itself learns of the shortfall from the
// return value. Pure C++11.
#pragma once

#include <embedbench_device.h>

class FloodModel : public ebdev::Device {
 public:
  // Printable on purpose so the log shows the bytes themselves.
  static const size_t kReplyLength = 12;  // "0123456789AB"

  void reset() override;
  void serialIn(const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint32_t sent_ = 0;
  uint32_t refused_ = 0;
};
