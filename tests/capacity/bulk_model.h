// Bulk sensor model: holds a 30-byte sample block and ships it as
// segments of a format that itself defines segmentation
// ("acme.bulk.1": [index, total, data...]), sized to the environment's
// negotiated frame capacity. It also has one ATOMIC 128-bit snapshot frame
// ("acme.snap.1") that must never be split: when it does not fit, it is
// not sent and the refusal is counted. Pure C++11.
#pragma once

#include <embedbench_device.h>

class BulkSensorModel : public ebdev::Device {
 public:
  static const uint8_t kBus = 0;
  static const size_t kSampleBytes = 30;
  static const size_t kSegmentHeader = 2;  // [index, total]
  static const size_t kSnapshotBytes = 16;
  static const uint8_t kChannelSeed = 0;      // world sets the sample pattern
  static const uint8_t kChannelShip = 1;      // world requests a shipment
  static const uint8_t kChannelSnapshot = 2;  // world requests the snapshot

  void reset() override;

  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void resolve();
  void ship();
  void snapshot();

  uint8_t samples_[kSampleBytes] = {0};
  uint16_t segmentFormat_ = 0;
  uint16_t snapshotFormat_ = 0;
  bool resolved_ = false;
  uint32_t framesSent_ = 0;
  uint32_t unsent_ = 0;
};
