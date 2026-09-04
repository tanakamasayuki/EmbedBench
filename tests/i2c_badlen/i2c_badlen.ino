// Blocker check on the host core: a model claiming more bytes than the
// master's buffer is diagnosed and treated as having supplied nothing, so
// the log never describes bytes outside the buffer.
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include "badlen_model.h"

static BadLengthModel model;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
};

static DraftPort draftPort;

static uint8_t devWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return model.i2cWrite(data, len, xfer);
}
static size_t devRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return model.i2cRead(data, len, xfer);
}
// [adapter end]

static size_t appGot = 0;

static void runOnce(char* out, size_t cap) {
  model.reset();
  ebd::runBegin(1000);
  appGot = Wire.requestFrom(static_cast<uint16_t>(0x60),
                            static_cast<size_t>(2), true);
  while (Wire.available()) Wire.read();
  char dump[40];
  model.dump(dump, sizeof(dump));
  ebd::dumpf("%s", dump);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[768];
static char run2[768];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start i2c_badlen");
  Wire.begin(21, 22, 400000);
  model.attach(&draftPort);
  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x60, ops);

  runOnce(run1, sizeof(run1));
  const ebd::Stats s = ebd::stats();
  Serial.printf("values got=%u diag=%u\n", static_cast<unsigned>(appGot),
                s.diagCount);
  Serial.print(run1);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
