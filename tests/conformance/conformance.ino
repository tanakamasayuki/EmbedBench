// The standard conformance scenario on the host core, driven through the
// draft core. Same probe, same sequence, same verdict as the native run.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostUart.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include <conformance_probe.h>

static ConformanceProbe probe;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    ebd::pinInject(ebd::Origin::kDev, 27, level);
  }
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebd::uartInject(ebd::Origin::kDev, data, len);
  }
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

static uint8_t devWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return probe.i2cWrite(data, len, xfer);
}
static size_t devRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return probe.i2cRead(data, len, xfer);
}
static void devUartTx(const uint8_t* data, size_t len, void*) {
  probe.serialIn(data, len);
}
static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return probe.channelWrite(channel, data, len);
}
static void advanceDevice(uint64_t nowUs, void*) { probe.advanceTo(nowUs); }
// [adapter end]

static void runScenario() {
  probe.reset();
  uint8_t drain[16];
  while (Serial1.readTx(drain, sizeof(drain)) > 0) {
  }
  ebd::runBegin(1000);

  Wire.beginTransmission(0x70);
  Wire.write(0x11);
  Wire.write(0x22);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(0x70), static_cast<size_t>(2), true);
  while (Wire.available()) Wire.read();
  Serial1.write(0x11);
  Serial1.write(0x22);
  delay(1);   // advanceTo with time moving forward
  delay(1);   // and again, so a repeat can be observed at the boundary
  const uint8_t go[1] = {0x01};
  ebd::chanWrite(ebd::Origin::kDir, ConformanceProbe::kChannelProbePort, go,
                 sizeof(go));

  ebd::runEnd();
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start conformance");
  Wire.begin(21, 22, 400000);
  Serial1.begin(9600);
  pinMode(27, INPUT);
  probe.attach(&draftPort);
  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x70, ops);
  ebd::bindUartDevice(&devUartTx);
  ebd::setChannelHandler(&devChannel);
  ebd::bindTickDevice(&advanceDevice);

  runScenario();
  char text[48];
  probe.dump(text, sizeof(text));
  Serial.printf("conformance ok=%d checks=%03X violations=%u dump=<%s>\n",
                probe.conforms() ? 1 : 0, probe.checks(), probe.violations(),
                text);
  const ebd::Stats s = ebd::stats();
  Serial.printf("device_depth=%u\n", s.maxDeviceDepth);
  Serial.println("TEST done");
}

void loop() { delay(10); }
