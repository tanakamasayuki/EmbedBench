// Bulk sensor model: holds a 32-byte sample block and ships it out as
// frames, split to whatever per-call frame capacity the environment
// negotiates via HostPort::maxFrameBits. The model hard-codes no size.
// Pure C++11.
#pragma once

#include <embedbench_device.h>

class BulkSensorModel : public ebdev::Device {
 public:
  static const uint8_t kBus = 0;
  static const size_t kSampleBytes = 32;
  static const uint8_t kChannelSeed = 0;  // world sets the sample pattern
  static const uint8_t kChannelShip = 1;  // world requests a shipment

  void reset() override;

  void channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void ship();

  uint8_t samples_[kSampleBytes] = {0};
  uint16_t dataFormatId_ = 0;
  bool resolved_ = false;
  uint32_t framesSent_ = 0;
};
