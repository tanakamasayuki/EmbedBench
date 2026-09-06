// The standard conformance scenario on the host core, driven through the
// draft core. Same probe, same sequence, same verdict as the native run.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_internals.h>
#include <HostBus.h>
#include <HostUart.h>
#include <Wire.h>
#include <string.h>

#include <conformance_probe.h>

static ConformanceProbe probe;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    ebhost::pinInject(ebhost::Origin::kDev, 27, level);
  }
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebhost::uartInject(ebhost::Origin::kDev, data, len);
  }
  bool analogOut(uint8_t, uint16_t raw) override {
    ebhost::analogInject(ebhost::Origin::kDev, 8, raw);
    return true;
  }
  bool requestWake(uint64_t whenUs) override {
    return ebhost::requestWake(whenUs);
  }
  bool diagnose(const char* text) override {
    ebhost::deviceNote(text);
    return true;
  }
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
  ebhost::runBegin(1000);

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
  ebhost::chanWrite(ebhost::Origin::kDir, ConformanceProbe::kChannelProbePort, go,
                 sizeof(go));
  delay(1);  // let the wake the probe asked for arrive

  ebhost::runEnd();
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start conformance");
  Wire.begin(21, 22, 400000);
  Serial1.begin(9600);
  pinMode(27, INPUT);
  probe.attach(&draftPort);
  const ebhost::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebhost::bindWireDevice(0x70, ops);
  ebhost::bindUartDevice(&devUartTx);
  ebhost::setChannelHandler(&devChannel);
  ebhost::bindTickDevice(&advanceDevice);

  runScenario();
  char text[48];
  probe.dump(text, sizeof(text));
  Serial.printf("conformance ok=%d checks=%03X violations=%u dump=<%s>\n",
                probe.conforms() ? 1 : 0, probe.checks(), probe.violations(),
                text);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("device_depth=%u\n", s.maxDeviceDepth);
  Serial.println("TEST done");
}

void loop() { delay(10); }
