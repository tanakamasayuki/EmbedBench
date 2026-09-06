// Capacity negotiation on the host core: the bulk model splits to the
// environment's published frame capacity, and an oversized application
// frame is rejected whole with a diagnostic — never silently truncated.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_internals.h>
#include <string.h>

#include "bulk_model.h"

static BulkSensorModel model;
static uint32_t deviceFrameCalls = 0;

// --- Platform adapter -------------------------------------------------------
// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebhost::frameRx(ebhost::Origin::kDev, bus, format, data, bits);
  }
  uint16_t formatId(const char* name, uint32_t schema) override {
    return ebhost::registerFormat(name, schema);
  }
  uint32_t maxFrameBits(uint8_t) override { return ebhost::frameCapacityBits(); }
};

static DraftPort draftPort;

static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return model.channelWrite(channel, data, len);
}
static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  ++deviceFrameCalls;
  model.frameIn(bus, format, data, bits);
}
// [adapter end]

// --- One run ---------------------------------------------------------------
static void runOnce(char* out, size_t cap) {
  model.reset();
  deviceFrameCalls = 0;

  ebhost::runBegin(1000);
  const uint8_t seed[1] = {0x10};
  ebhost::chanWrite(ebhost::Origin::kDir, BulkSensorModel::kChannelSeed, seed, 1);
  const uint8_t go[1] = {0x01};
  ebhost::chanWrite(ebhost::Origin::kDir, BulkSensorModel::kChannelShip, go, 1);

  // Device-side atomic frame that does not fit: not split, not sent,
  // counted by the model as unsent (channel 2 asks for a snapshot).
  const uint8_t snap[1] = {0x01};
  ebhost::chanWrite(ebhost::Origin::kDir, BulkSensorModel::kChannelSnapshot, snap, 1);

  // Application shim violating the negotiated limit: rejected whole, the
  // device never sees it, and a diagnostic marks the spot.
  uint8_t oversize[16] = {0};
  ebhost::frameTx(ebhost::Origin::kApp, 0, ebhost::registerFormat("acme.bulk.1", 0x0101),
               oversize, 128);

  char text[32];
  model.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  ebhost::runEnd();
  ebhost::formatTrace(out, cap);
}

static char run1[1024];
static char run2[1024];
static char run3[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start capacity");

  model.attach(&draftPort);
  ebhost::setChannelHandler(&devChannel);
  ebhost::bindFrameDevice(&devFrame);

  runOnce(run1, sizeof(run1));
  Serial.printf("values capacity=%u device_calls=%u\n",
                static_cast<unsigned>(ebhost::frameCapacityBits()),
                deviceFrameCalls);
  Serial.print(run1);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);

  runOnce(run2, sizeof(run2));
  runOnce(run3, sizeof(run3));
  Serial.printf("run2_same=%d run3_same=%d\n",
                strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);

  Serial.println("TEST done");
}

void loop() { delay(10); }
