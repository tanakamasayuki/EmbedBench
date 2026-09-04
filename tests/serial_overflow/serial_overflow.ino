// Blocker check on the host core: the sketch shrinks the UART receive
// queue, the device replies with more than fits, and the environment
// delivers the accepted prefix, drops the rest, diagnoses it, and returns
// false to the device.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostUart.h>
#include <embedbench_draft.h>
#include <string.h>

#include "flood_model.h"

static FloodModel model;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebd::uartInject(ebd::Origin::kDev, data, len);
  }
};

static DraftPort draftPort;

static void devUartTx(const uint8_t* data, size_t len, void*) {
  model.serialIn(data, len);
}
// [adapter end]

static size_t appRead = 0;
static int appLeftover = 0;
static char appPrefix[9] = {0};

static void runOnce(char* out, size_t cap) {
  model.reset();
  uint8_t drain[32];
  while (Serial1.readTx(drain, sizeof(drain)) > 0) {
  }
  Serial1.flush();                // discard anything left in the rx queue
  Serial1.setRxBufferSize(8);     // room for eight of the twelve bytes
  memset(appPrefix, 0, sizeof(appPrefix));

  ebd::runBegin(1000);
  Serial1.print("go");
  uint8_t got[8] = {0};
  appRead = Serial1.readBytes(got, sizeof(got));
  memcpy(appPrefix, got, sizeof(got));
  appLeftover = Serial1.available();
  char dump[40];
  model.dump(dump, sizeof(dump));
  ebd::dumpf("%s", dump);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1024];
static char run2[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start serial_overflow");
  Serial1.begin(9600);
  Serial1.setTimeout(10);
  model.attach(&draftPort);
  ebd::bindUartDevice(&devUartTx);

  runOnce(run1, sizeof(run1));
  Serial.printf("values read=%u prefix=%s leftover=%d\n",
                static_cast<unsigned>(appRead), appPrefix, appLeftover);
  Serial.print(run1);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u diag=%u\n", s.events, s.diagCount);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
