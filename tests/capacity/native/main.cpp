// Native proof that size limits negotiate per environment: the same model
// splits its 32-byte block into 4 frames on a small port, 1 frame on a
// large port, and stays inert on a port with no frame routing.
#include <stdio.h>
#include <string.h>

#include "../bulk_model.h"

namespace {

struct CountingPort : public ebdev::HostPort {
  uint32_t capacityBits;
  uint32_t frames = 0;
  size_t totalBytes = 0;
  uint8_t byteSum = 0;
  size_t maxSeenBytes = 0;

  explicit CountingPort(uint32_t bits) : capacityBits(bits) {}

  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  void serialOut(const uint8_t*, size_t) override {}
  uint16_t formatId(const char*) override { return 1; }
  uint32_t maxFrameBits(uint8_t) override { return capacityBits; }
  void frameOut(uint8_t, uint16_t, const uint8_t* data,
                size_t bits) override {
    const size_t bytes = (bits + 7) / 8;
    ++frames;
    totalBytes += bytes;
    if (bytes > maxSeenBytes) maxSeenBytes = bytes;
    for (size_t i = 0; i < bytes; ++i) {
      byteSum = static_cast<uint8_t>(byteSum + data[i]);
    }
  }
};

struct PlainPort : public ebdev::HostPort {
  uint32_t frames = 0;
  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  void serialOut(const uint8_t*, size_t) override {}
  void frameOut(uint8_t, uint16_t, const uint8_t*, size_t) override {
    ++frames;
  }
};

void runShipment(ebdev::HostPort* port, BulkSensorModel* model) {
  model->attach(port);
  model->reset();
  const uint8_t seed[1] = {0x10};
  model->channelWrite(BulkSensorModel::kChannelSeed, seed, 1);
  model->channelWrite(BulkSensorModel::kChannelShip, nullptr, 0);
}

}  // namespace

int main() {
  printf("NATIVE start\n");

  // Small environment: 64 bits per call -> four 8-byte frames.
  CountingPort small(64);
  BulkSensorModel model;
  runShipment(&small, &model);
  char d1[32];
  model.dump(d1, sizeof(d1));
  printf("small frames=%u bytes=%zu max_chunk=%zu sum=%02X dump=<%s>\n",
         small.frames, small.totalBytes, small.maxSeenBytes, small.byteSum,
         d1);

  // Large environment: 4096 bits per call -> one 32-byte frame. The same
  // unmodified model adapts; the payload checksum matches either way.
  CountingPort large(4096);
  runShipment(&large, &model);
  char d2[32];
  model.dump(d2, sizeof(d2));
  printf("large frames=%u bytes=%zu max_chunk=%zu sum=%02X dump=<%s>\n",
         large.frames, large.totalBytes, large.maxSeenBytes, large.byteSum,
         d2);

  // No frame routing (default maxFrameBits = 0): safely inert.
  PlainPort plain;
  runShipment(&plain, &model);
  char d3[32];
  model.dump(d3, sizeof(d3));
  printf("noroute frames=%u dump=<%s>\n", plain.frames, d3);

  printf("NATIVE done\n");
  return 0;
}
