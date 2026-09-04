// Re-entrancy contract on the host core: the sensor raises its IRQ inside
// i2cRead, the application ISR reads the sensor again, and the draft core
// records the line change at once but runs the ISR only after the first
// read has fully completed — the device is never re-entered.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostInterrupt.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include "irq_model.h"

static IrqSensorModel model;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t line, uint8_t level) override {
    if (line == IrqSensorModel::kLineIrq) {
      ebd::pinInject(ebd::Origin::kDev, 27, level);
    }
  }
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

// --- Application ------------------------------------------------------------
static volatile uint32_t isrReads = 0;

static uint16_t readSensor() {
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(0x48), static_cast<size_t>(2), true);
  const int first = Wire.read();
  const int second = Wire.read();
  return static_cast<uint16_t>((first << 8) | second);
}

static void onIrq() {
  readSensor();
  ++isrReads;
}

static uint16_t appValue = 0;

static void runOnce(char* out, size_t cap) {
  HostArduino::resetInterrupts();
  HostArduino::setPinValue(27, LOW);
  model.reset();
  isrReads = 0;
  ebd::runBegin(1000);
  attachInterrupt(27, &onIrq, RISING);
  appValue = readSensor();
  char text[40];
  model.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1600];
static char run2[1600];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start reentry");
  Wire.begin(21, 22, 400000);
  pinMode(27, INPUT);
  model.attach(&draftPort);
  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x48, ops);

  runOnce(run1, sizeof(run1));
  const ebd::Stats s = ebd::stats();
  Serial.printf("values app=%04X isr_reads=%u deferred=%u device_depth=%u\n",
                appValue, isrReads, s.deferredIsrs, s.maxDeviceDepth);
  Serial.print(run1);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
