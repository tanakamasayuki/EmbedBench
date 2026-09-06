// The frame-path node model on the host core. The app-side protocol shim
// (standing in for the host variant of an IR/PIO/RF driver) hands logical
// frames to the environment instead of bit-banging pins; the device's
// telemetry frame comes back through the receiver shim, all recorded.
#include <Arduino.h>
#include <EmbedBench.h>
#include <string.h>

#include "node_model.h"

static RemoteNodeModel node;

// --- Platform adapter and shims --------------------------------------------
// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebhost::frameRx(ebhost::Origin::kDev, bus, format, data, bits);
  }
  uint16_t formatId(const char* name, uint32_t schema) override {
    return ebhost::registerFormat(name, schema);
  }
};

static uint16_t commandFormat = 0;
static uint16_t telemetryFormat = 0;

static DraftPort draftPort;

static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  node.frameIn(bus, format, data, bits);
}
static void advanceDevice(uint64_t nowUs, void*) { node.advanceTo(nowUs); }

// Application-side shim: what the host variant of a protocol driver does
// instead of encoding to a waveform.
static volatile bool gotTelemetry = false;
static uint8_t telemetry[2] = {0, 0};

static void appFrameReceiver(uint8_t, uint16_t format, const uint8_t* data,
                             size_t bits, void*) {
  if (format == telemetryFormat && bits == 16) {
    telemetry[0] = data[0];
    telemetry[1] = data[1];
    gotTelemetry = true;
  }
}

static void appSendCommand(uint8_t address, uint8_t command) {
  const uint8_t frame[2] = {address, command};
  ebhost::frameTx(ebhost::Origin::kApp, RemoteNodeModel::kBus, commandFormat, frame,
               16);
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

  ebhost::runBegin(1000);
  appScenario();
  char text[40];
  node.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  ebhost::runEnd();
  ebhost::formatTrace(out, cap);
}

static char run1[768];
static char run2[768];
static char run3[768];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start frame_port");

  node.attach(&draftPort);
  ebhost::bindFrameDevice(&devFrame);
  ebhost::setFrameReceiver(&appFrameReceiver);
  ebhost::bindTickDevice(&advanceDevice);
  // The application shim resolves the same names the model does.
  commandFormat = ebhost::registerFormat("acme.node.1", ebdev::schemaFingerprint("u8 addr,u8 cmd"));
  telemetryFormat = ebhost::registerFormat("acme.tele.1", ebdev::schemaFingerprint("u8 addr,u8 power"));

  runOnce(run1, sizeof(run1));
  Serial.printf("values got=%d telemetry=%02X%02X\n", gotTelemetry ? 1 : 0,
                telemetry[0], telemetry[1]);
  Serial.print(run1);
  const ebhost::Stats s = ebhost::stats();
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
