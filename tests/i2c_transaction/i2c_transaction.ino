// I2C transaction context on the host core: Wire.endTransmission(false)
// followed by requestFrom is delivered to the model as a write without
// STOP and a read under repeated start; the same read after a STOP is a
// standalone read the device refuses.
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include <regmap_model.h>

static RegisterMapModel model;

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

static size_t appRsLen = 0;
static int appRsVal = -1;
static size_t appPlainLen = 0;

static void runOnce(char* out, size_t cap) {
  model.reset();
  ebd::runBegin(1000);

  // Pointer write without STOP, then read: repeated start.
  Wire.beginTransmission(0x50);
  Wire.write(0x01);
  Wire.endTransmission(false);
  appRsLen = Wire.requestFrom(static_cast<uint16_t>(0x50),
                              static_cast<size_t>(1), true);
  appRsVal = Wire.read();

  // Pointer write with STOP, then read: standalone read, refused.
  Wire.beginTransmission(0x50);
  Wire.write(0x01);
  Wire.endTransmission(true);
  appPlainLen = Wire.requestFrom(static_cast<uint16_t>(0x50),
                                 static_cast<size_t>(1), true);

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
  Serial.println("TEST start i2c_transaction");

  Wire.begin(21, 22, 400000);
  model.attach(&draftPort);
  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x50, ops);

  runOnce(run1, sizeof(run1));
  Serial.printf("values rs_len=%u rs_val=%02X plain_len=%u\n",
                static_cast<unsigned>(appRsLen), appRsVal,
                static_cast<unsigned>(appPlainLen));
  Serial.print(run1);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
