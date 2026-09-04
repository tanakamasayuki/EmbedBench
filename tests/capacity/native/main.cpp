// Native proof that size limits negotiate per environment and that
// atomicity holds: the segmented format adapts (5 segments on a 64-bit
// port, 1 on a 4096-bit port), the atomic snapshot is refused rather than
// split where it does not fit, and a port with no frame routing leaves
// the model inert.
#include <stdio.h>
#include <string.h>

#include "../bulk_model.h"

namespace {

struct CountingPort : public ebdev::HostPort {
  uint32_t capacityBits;
  uint32_t frames = 0;
  uint32_t refused = 0;
  size_t totalBytes = 0;
  uint8_t byteSum = 0;
  size_t maxSeenBytes = 0;

  explicit CountingPort(uint32_t bits) : capacityBits(bits) {}

  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  uint16_t formatId(const char* name, uint32_t) override {
    return strcmp(name, "acme.bulk.1") == 0 ? 1 : 2;
  }
  uint32_t maxFrameBits(uint8_t) override { return capacityBits; }
  bool frameOut(uint8_t, uint16_t, const uint8_t* data,
                size_t bits) override {
    if (bits > capacityBits) {
      ++refused;
      return false;
    }
    const size_t bytes = ebdev::frameBytes(bits);
    ++frames;
    totalBytes += bytes;
    if (bytes > maxSeenBytes) maxSeenBytes = bytes;
    for (size_t i = 0; i < bytes; ++i) {
      byteSum = static_cast<uint8_t>(byteSum + data[i]);
    }
    return true;
  }
};

struct PlainPort : public ebdev::HostPort {
  uint32_t frames = 0;
  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool frameOut(uint8_t, uint16_t, const uint8_t*, size_t) override {
    ++frames;
    return true;
  }
};

void runShipment(ebdev::HostPort* port, BulkSensorModel* model) {
  model->attach(port);
  model->reset();
  const uint8_t seed[1] = {0x10};
  model->channelWrite(BulkSensorModel::kChannelSeed, seed, 1);
  model->channelWrite(BulkSensorModel::kChannelShip, nullptr, 0);
  model->channelWrite(BulkSensorModel::kChannelSnapshot, nullptr, 0);
}

}  // namespace

int main() {
  printf("NATIVE start\n");

  CountingPort small(64);
  BulkSensorModel model;
  runShipment(&small, &model);
  char d1[40];
  model.dump(d1, sizeof(d1));
  printf("small frames=%u refused=%u bytes=%zu max_chunk=%zu sum=%02X dump=<%s>\n",
         small.frames, small.refused, small.totalBytes, small.maxSeenBytes,
         small.byteSum, d1);

  CountingPort large(4096);
  runShipment(&large, &model);
  char d2[40];
  model.dump(d2, sizeof(d2));
  printf("large frames=%u refused=%u bytes=%zu max_chunk=%zu sum=%02X dump=<%s>\n",
         large.frames, large.refused, large.totalBytes, large.maxSeenBytes,
         large.byteSum, d2);

  PlainPort plain;
  runShipment(&plain, &model);
  char d3[40];
  model.dump(d3, sizeof(d3));
  printf("noroute frames=%u dump=<%s>\n", plain.frames, d3);

  printf("NATIVE done\n");
  return 0;
}
