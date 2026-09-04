// The frame-path node model on the host core. The app-side protocol shim
// (standing in for the host variant of an IR/PIO/RF driver) hands logical
// frames to the environment instead of bit-banging pins; the device's
// telemetry frame comes back through the receiver shim, all recorded.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_draft.h>
#include <string.h>

#include "node_model.h"

static RemoteNodeModel node;

// --- Platform adapter and shims --------------------------------------------
// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  void serialOut(const uint8_t*, size_t) override {}
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebd::frameRx(ebd::Origin::kDev, bus, format, data, bits);
  }
};

static DraftPort draftPort;

static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  node.frameIn(bus, format, data, bits);
}
static void onTick(uint32_t, void*) { node.advanceTo(ebd::nowUs()); }

// Application-side shim: what the host variant of a protocol driver does
// instead of encoding to a waveform.
static volatile bool gotTelemetry = false;
static uint8_t telemetry[2] = {0, 0};

static void appFrameReceiver(uint8_t, uint16_t format, const uint8_t* data,
                             size_t bits, void*) {
  if (format == RemoteNodeModel::kFormatTelemetry && bits == 16) {
    telemetry[0] = data[0];
    telemetry[1] = data[1];
    gotTelemetry = true;
  }
}

static void appSendCommand(uint8_t address, uint8_t command) {
  const uint8_t frame[2] = {address, command};
  ebd::frameTx(ebd::Origin::kApp, RemoteNodeModel::kBus,
               RemoteNodeModel::kFormatCommand, frame, 16);
}
// [adapter end]

// --- Application ------------------------------------------------------------
static void appScenario() {
  appSendCommand(0x05, 0x08);  // foreign address: the node ignores it
  appSendCommand(0x04, 0x08);  // our node: power on, reply in 1,000 us
  delay(2);                    // the telemetry frame arrives at tick 1
}

// --- One run ---------------------------------------------------------------
static void runOnce(char* out, size_t cap) {
  node.reset();
  gotTelemetry = false;
  telemetry[0] = 0;
  telemetry[1] = 0;

  ebd::runBegin(1000);
  appScenario();
  char text[40];
  node.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[768];
static char run2[768];
static char run3[768];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start frame_port");

  node.attach(&draftPort);
  ebd::bindFrameDevice(&devFrame);
  ebd::setFrameReceiver(&appFrameReceiver);
  ebd::setTickHandler(&onTick);

  runOnce(run1, sizeof(run1));
  Serial.printf("values got=%d telemetry=%02X%02X\n", gotTelemetry ? 1 : 0,
                telemetry[0], telemetry[1]);
  Serial.print(run1);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);

  runOnce(run2, sizeof(run2));
  runOnce(run3, sizeof(run3));
  Serial.printf("run2_same=%d run3_same=%d\n",
                strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);

  Serial.println("TEST done");
}

void loop() { delay(10); }
