// Re-entrancy contract on every device path of the host draft core:
// lineIn (via pin forwarding), advanceTo (via the tick device binding),
// frameIn (via the application shim reacting to a device frame), and
// i2cRead — plus the deferral-capacity contract when a device raises more
// interrupts inside one call than the environment can hold.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostInterrupt.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include "paths_model.h"

static PathsModel model;
static uint16_t statusFormat = 0;
static uint16_t commandFormat = 0;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t line, uint8_t level) override {
    if (line == PathsModel::kLineIrq) ebd::pinInject(ebd::Origin::kDev, 27, level);
  }
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
static void forwardPins(uint8_t pin, uint8_t value, void*) {
  if (pin == 4) model.lineIn(PathsModel::kLineTrigger, value);
}
static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  model.frameIn(bus, format, data, bits);
}
static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return model.channelWrite(channel, data, len);
}
static void advanceDevice(uint64_t nowUs, void*) { model.advanceTo(nowUs); }

// Application shim: a device status frame makes the app send a command.
static void appFrameReceiver(uint8_t, uint16_t format, const uint8_t*, size_t,
                             void*) {
  if (format == statusFormat) {
    const uint8_t cmd[1] = {0x01};
    ebd::frameTx(ebd::Origin::kApp, 0, commandFormat, cmd, 8);
  }
}
// [adapter end]

// --- Application ------------------------------------------------------------
static uint16_t readSensor() {
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(0x48), static_cast<size_t>(2), true);
  const int first = Wire.read();
  const int second = Wire.read();
  return static_cast<uint16_t>((first << 8) | second);
}
static void onIrqRead() { readSensor(); }
static volatile uint32_t irqCount = 0;
static void onIrqCount() { ++irqCount; }

static void runOnce(char* out, size_t cap) {
  HostArduino::resetInterrupts();
  HostArduino::setPinValue(4, LOW);
  HostArduino::setPinValue(27, LOW);
  model.reset();
  irqCount = 0;
  ebd::runBegin(1000);

  attachInterrupt(27, &onIrqRead, RISING);
  digitalWrite(4, HIGH);  // lineIn path: the device pulses IRQ inside lineIn
  delay(1);               // advanceTo path: status frame -> shim -> frameIn -> IRQ

  // Burst path: 5 pulses inside one read against a deferral capacity of 4.
  attachInterrupt(27, &onIrqCount, RISING);
  const uint8_t burst[1] = {5};
  ebd::chanWrite(ebd::Origin::kDir, PathsModel::kChannelBurst, burst, 1);
  readSensor();

  char text[48];
  model.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[4096];
static char run2[4096];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start reentry_paths");
  Wire.begin(21, 22, 400000);
  pinMode(4, OUTPUT);
  pinMode(27, INPUT);
  model.attach(&draftPort);
  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x48, ops);
  ebd::setPinWriteForward(&forwardPins);
  ebd::bindFrameDevice(&devFrame);
  ebd::setChannelHandler(&devChannel);
  ebd::bindTickDevice(&advanceDevice);
  ebd::setFrameReceiver(&appFrameReceiver);
  statusFormat = ebd::registerFormat("acme.stat.1", ebdev::schemaFingerprint("u8 hi,u8 lo"));
  commandFormat = ebd::registerFormat("acme.cmd.1", ebdev::schemaFingerprint("u8 cmd"));

  runOnce(run1, sizeof(run1));
  const ebd::Stats s = ebd::stats();
  Serial.printf("values irq_count=%u deferred_isrs=%u deferred_frames=%u dropped=%u "
                "device_depth=%u capacity=%u\n",
                irqCount, s.deferredIsrs, s.deferredFrames, s.deferredDropped,
                s.maxDeviceDepth, static_cast<unsigned>(ebd::deferralCapacity()));
  Serial.print(run1);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
