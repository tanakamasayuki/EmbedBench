// Bulk sensor model implementation. Pure C++11.
#include "bulk_model.h"

#include <stdio.h>

void BulkSensorModel::reset() {
  for (size_t i = 0; i < kSampleBytes; ++i) samples_[i] = 0;
  dataFormatId_ = 0;
  resolved_ = false;
  framesSent_ = 0;
}

void BulkSensorModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  if (channel == kChannelSeed && len == 1) {
    for (size_t i = 0; i < kSampleBytes; ++i) {
      samples_[i] = static_cast<uint8_t>(data[0] + i);
    }
  } else if (channel == kChannelShip) {
    ship();
  }
}

void BulkSensorModel::ship() {
  if (port() == nullptr) return;
  if (!resolved_) {
    dataFormatId_ = port()->formatId("bulk.data");
    resolved_ = true;
  }
  // The per-call capacity is the environment's property, negotiated here
  // rather than fixed in the model or the interface.
  const uint32_t maxBits = port()->maxFrameBits(kBus);
  if (dataFormatId_ == 0 || maxBits < 8) return;  // no frame routing
  const size_t maxBytes = maxBits / 8;
  size_t offset = 0;
  while (offset < kSampleBytes) {
    const size_t chunk =
        kSampleBytes - offset < maxBytes ? kSampleBytes - offset : maxBytes;
    port()->frameOut(kBus, dataFormatId_, samples_ + offset, chunk * 8);
    ++framesSent_;
    offset += chunk;
  }
}

size_t BulkSensorModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "bulk sent=%u", framesSent_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
