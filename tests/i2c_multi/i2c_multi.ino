// I2C repeated start is a property of the bus, not of a device: a transfer
// to another address in between closes the sequence. Two repeated-start-
// requiring register-map models on 0x50 and 0x51 make the difference
// visible in the read results.
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include <regmap_model.h>

static RegisterMapModel deviceA;
static RegisterMapModel deviceB;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  void serialOut(const uint8_t*, size_t) override {}
};

static DraftPort draftPort;

static uint8_t devWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void* user) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return static_cast<RegisterMapModel*>(user)->i2cWrite(data, len, xfer);
}
static size_t devRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void* user) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return static_cast<RegisterMapModel*>(user)->i2cRead(data, len, xfer);
}
// [adapter end]

static size_t appBrokenLen = 0;
static size_t appProperLen = 0;

static void pointerWrite(uint8_t address, bool stop) {
  Wire.beginTransmission(address);
  Wire.write(0x01);
  Wire.endTransmission(stop);
}

static void runOnce(char* out, size_t cap) {
  deviceA.reset();
  deviceB.reset();
  ebd::runBegin(1000);

  // A leaves the bus open, B closes it with STOP, then A is read: no
  // repeated start for A any more.
  pointerWrite(0x50, false);
  pointerWrite(0x51, true);
  appBrokenLen = Wire.requestFrom(static_cast<uint16_t>(0x50),
                                  static_cast<size_t>(1), true);
  while (Wire.available()) Wire.read();

  // A leaves the bus open and is read next: a genuine repeated start.
  pointerWrite(0x50, false);
  appProperLen = Wire.requestFrom(static_cast<uint16_t>(0x50),
                                  static_cast<size_t>(1), true);
  while (Wire.available()) Wire.read();

  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1024];
static char run2[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start i2c_multi");
  Wire.begin(21, 22, 400000);
  deviceA.attach(&draftPort);
  deviceB.attach(&draftPort);
  const ebd::WireDeviceOps opsA = {&devWrite, &devRead, &deviceA};
  const ebd::WireDeviceOps opsB = {&devWrite, &devRead, &deviceB};
  ebd::bindWireDevice(0x50, opsA);
  ebd::bindWireDevice(0x51, opsB);

  runOnce(run1, sizeof(run1));
  Serial.printf("values broken_len=%u proper_len=%u\n",
                static_cast<unsigned>(appBrokenLen),
                static_cast<unsigned>(appProperLen));
  Serial.print(run1);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
