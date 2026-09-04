// Frame bit packing and atomicity on the host core: the environment
// records clean 12-bit frames, refuses one with dirty padding, accepts an
// empty frame as a trigger, and refuses the model's atomic 128-bit status
// frame instead of truncating or splitting it.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_draft.h>
#include <string.h>

#include "ir_model.h"

static IrReceiverModel model;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebd::frameRx(ebd::Origin::kDev, bus, format, data, bits);
  }
  uint16_t formatId(const char* name, uint32_t schema) override {
    return ebd::registerFormat(name, schema);
  }
  uint32_t maxFrameBits(uint8_t) override { return ebd::frameCapacityBits(); }
};

static DraftPort draftPort;

static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  model.frameIn(bus, format, data, bits);
}
static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return model.channelWrite(channel, data, len);
}
// [adapter end]

static void runOnce(char* out, size_t cap) {
  model.reset();
  ebd::runBegin(1000);
  const uint16_t ir = ebd::registerFormat("acme.ir.1", 0x000C);
  const uint8_t cmdAbc[2] = {0xAB, 0xC0};
  const uint8_t cmd123[2] = {0x12, 0x30};
  const uint8_t dirty[2] = {0xAB, 0xCF};
  ebd::frameTx(ebd::Origin::kApp, 0, ir, cmdAbc, 12);
  ebd::frameTx(ebd::Origin::kApp, 0, ir, cmd123, 12);
  ebd::frameTx(ebd::Origin::kApp, 0, ir, dirty, 12);   // refused: padding
  ebd::frameTx(ebd::Origin::kApp, 0, ir, nullptr, 0);  // empty trigger
  const uint8_t go[1] = {0x01};
  ebd::chanWrite(ebd::Origin::kDir, IrReceiverModel::kChannelStatus, go, 1);
  char text[48];
  model.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1024];
static char run2[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start frame_bits");
  model.attach(&draftPort);
  ebd::bindFrameDevice(&devFrame);
  ebd::setChannelHandler(&devChannel);

  runOnce(run1, sizeof(run1));
  Serial.print(run1);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
