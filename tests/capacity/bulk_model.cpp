// Bulk sensor model implementation. Pure C++11.
#include "bulk_model.h"

#include <stdio.h>

void BulkSensorModel::reset() {
  for (size_t i = 0; i < kSampleBytes; ++i) samples_[i] = 0;
  segmentFormat_ = 0;
  snapshotFormat_ = 0;
  resolved_ = false;
  framesSent_ = 0;
  unsent_ = 0;
}

void BulkSensorModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  segmentFormat_ = port()->formatId("acme.bulk.1", 0x0101);
  snapshotFormat_ = port()->formatId("acme.snap.1", 0x0201);
  resolved_ = true;
}

bool BulkSensorModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  if (channel == kChannelSeed && len == 1) {
    for (size_t i = 0; i < kSampleBytes; ++i) {
      samples_[i] = static_cast<uint8_t>(data[0] + i);
    }
    return true;
  }
  if (channel == kChannelShip) {
    ship();
    return true;
  }
  if (channel == kChannelSnapshot) {
    snapshot();
    return true;
  }
  return false;
}

// Segmentation is a property of the "acme.bulk.1" format, not of the
// interface: every emitted frame is a complete frame of that format.
void BulkSensorModel::ship() {
  resolve();
  if (port() == nullptr || segmentFormat_ == 0) return;
  const uint32_t maxBits = port()->maxFrameBits(kBus);
  const size_t maxBytes = maxBits / 8;
  if (maxBytes <= kSegmentHeader) return;
  const size_t dataPerSegment = maxBytes - kSegmentHeader;
  const size_t total = (kSampleBytes + dataPerSegment - 1) / dataPerSegment;
  uint8_t frame[64];
  size_t offset = 0;
  for (size_t index = 0; index < total; ++index) {
    const size_t chunk = kSampleBytes - offset < dataPerSegment
                             ? kSampleBytes - offset
                             : dataPerSegment;
    frame[0] = static_cast<uint8_t>(index);
    frame[1] = static_cast<uint8_t>(total);
    for (size_t i = 0; i < chunk; ++i) frame[kSegmentHeader + i] = samples_[offset + i];
    if (port()->frameOut(kBus, segmentFormat_, frame, (kSegmentHeader + chunk) * 8)) {
      ++framesSent_;
    } else {
      ++unsent_;
    }
    offset += chunk;
  }
}

// The snapshot is one atomic frame: it is sent whole or not at all.
void BulkSensorModel::snapshot() {
  resolve();
  if (port() == nullptr || snapshotFormat_ == 0) return;
  if (port()->frameOut(kBus, snapshotFormat_, samples_, kSnapshotBytes * 8)) {
    ++framesSent_;
  } else {
    ++unsent_;
  }
}

size_t BulkSensorModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "bulk sent=%u unsent=%u", framesSent_, unsent_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
