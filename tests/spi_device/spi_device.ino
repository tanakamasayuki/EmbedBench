// The composite SPI display model, unchanged, on the host core: the app
// drives DC over GPIO and transfers bytes over SPI; the model raises its
// busy line between the request and response of the refresh byte, and
// time releases it. Exchanges are logical byte streams by project stance.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <SPI.h>
#include <embedbench_draft.h>
#include <string.h>

#include "display_model.h"

static SpiDisplayModel display;

// --- Platform adapter: pure device IF <-> draft core ----------------------
// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t line, uint8_t level) override {
    // The display's busy line is wired to pin 26 here.
    const uint8_t pin = line == SpiDisplayModel::kLineBusy ? 26 : 0xFF;
    if (pin != 0xFF) ebd::pinInject(ebd::Origin::kDev, pin, level);
  }
  void serialOut(const uint8_t*, size_t) override {}
};

static DraftPort draftPort;

static uint8_t devTransfer(uint8_t mosi, void*) {
  return display.spiTransfer(mosi);
}
static void forwardPins(uint8_t pin, uint8_t value, void*) {
  // The app's DC line (pin 4) is input line 0 of the display.
  if (pin == 4) display.lineIn(SpiDisplayModel::kLineDc, value);
}
static void advanceDevice(uint64_t nowUs, void*) { display.advanceTo(nowUs); }
// [adapter end]

// --- Application: unmodified Arduino code ----------------------------------
static uint8_t appAck = 0;
static uint8_t appSum1 = 0;
static uint8_t appSum2 = 0;
static uint32_t appBusyReads = 0;

static void appScenario() {
  digitalWrite(4, LOW);  // command phase
  appAck = SPI.transfer(0x2C);
  digitalWrite(4, HIGH);  // data phase
  appSum1 = SPI.transfer(0x10);
  appSum2 = SPI.transfer(0x20);
  digitalWrite(4, LOW);
  SPI.transfer(0xFF);  // refresh: busy rises inside this transfer
  uint32_t reads = 0;
  while (digitalRead(26) == HIGH && reads < 100) {
    ++reads;
    delay(1);
  }
  ++reads;  // the final LOW read that ended the loop
  appBusyReads = reads;
}

// --- One run ---------------------------------------------------------------
static void runOnce(char* out, size_t cap) {
  display.reset();
  HostArduino::setPinValue(4, LOW);
  HostArduino::setPinValue(26, LOW);

  ebd::runBegin(1000);
  appScenario();
  char text[48];
  display.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1024];
static char run2[1024];
static char run3[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start spi_device");

  SPI.begin(18, 19, 23, 5);
  pinMode(4, OUTPUT);
  pinMode(26, INPUT);

  display.attach(&draftPort);
  ebd::bindSpiDevice(&devTransfer);
  ebd::setPinWriteForward(&forwardPins);
  ebd::bindTickDevice(&advanceDevice);

  runOnce(run1, sizeof(run1));
  Serial.printf("values ack=%02X s1=%02X s2=%02X busy_reads=%u\n", appAck,
                appSum1, appSum2, appBusyReads);
  Serial.print(run1);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u resp_lines=%u diag=%u\n",
                s.events, s.dropped,
                static_cast<unsigned>(ebd::respLineCount()), s.diagCount);

  runOnce(run2, sizeof(run2));
  runOnce(run3, sizeof(run3));
  Serial.printf("run2_same=%d run3_same=%d\n",
                strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);

  Serial.println("TEST done");
}

void loop() { delay(10); }
