// The interned format registry on the host core: names registered by the
// shim and resolved by the device meet in one environment registry, the
// trace prints names, a same-name re-registration is idempotent, a frame
// on the wrong logical bus is ignored, and a full registry diagnoses.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_draft.h>
#include <string.h>

#include "named_node_model.h"

static NamedNodeModel node;
static uint16_t overflowId = 0xFFFF;

// --- Platform adapter and shims --------------------------------------------
// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebd::frameRx(ebd::Origin::kDev, bus, format, data, bits);
  }
  uint16_t formatId(const char* name, uint32_t schema) override {
    return ebd::registerFormat(name, schema);
  }
};

static DraftPort draftPort;

static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  node.frameIn(bus, format, data, bits);
}

static void advanceDevice(uint64_t nowUs, void*) { node.advanceTo(nowUs); }

static void onTick(uint32_t tick, void*) {
  if (tick == 2) {
    // Director probe: fill the registry, then overflow it once.
    ebd::registerFormat("acme.xa.1", 0);
    ebd::registerFormat("acme.xb.1", 0);
    ebd::registerFormat("acme.xc.1", 0);
    ebd::registerFormat("acme.xd.1", 0);
    ebd::registerFormat("acme.xe.1", 0);
    overflowId = ebd::registerFormat("acme.over.1", 0);
  }
}

static volatile bool gotTelemetry = false;
static uint8_t telemetry[2] = {0, 0};

static void appFrameReceiver(uint8_t, uint16_t, const uint8_t* data,
                             size_t bits, void*) {
  if (bits == 16) {
    telemetry[0] = data[0];
    telemetry[1] = data[1];
    gotTelemetry = true;
  }
}

static void appSendFrame(uint8_t bus, uint16_t format, uint8_t b0,
                         uint8_t b1) {
  const uint8_t frame[2] = {b0, b1};
  ebd::frameTx(ebd::Origin::kApp, bus, format, frame, 16);
}
// [adapter end]

// --- One run ---------------------------------------------------------------
static void runOnce(char* out, size_t cap, uint16_t idCmd, uint16_t idVendor) {
  node.reset();
  gotTelemetry = false;
  telemetry[0] = 0;
  telemetry[1] = 0;
  overflowId = 0xFFFF;

  ebd::runBegin(1000);
  appSendFrame(0, idCmd, 0x05, 0x08);     // foreign address
  appSendFrame(0, idCmd, 0x04, 0x08);     // ours: power on
  appSendFrame(0, idVendor, 0x04, 0x08);  // other protocol, same payload
  appSendFrame(1, idCmd, 0x04, 0x00);     // right protocol, wrong bus
  delay(2);
  char text[40];
  node.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1024];
static char run2[1024];
static char run3[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start format_registry");

  node.attach(&draftPort);
  ebd::bindFrameDevice(&devFrame);
  ebd::setFrameReceiver(&appFrameReceiver);
  ebd::bindTickDevice(&advanceDevice);
  ebd::setTickHandler(&onTick);

  // The shim registers by name; re-registering the same name is
  // idempotent; an independent vendor name gets its own id.
  const uint16_t idCmd = ebd::registerFormat("acme.node.1", 0x0001);
  const uint16_t idTel = ebd::registerFormat("acme.tele.1", 0x0001);
  const uint16_t idAgain = ebd::registerFormat("acme.node.1", 0x0001);
  const uint16_t idVendor = ebd::registerFormat("vend.cal.1", 0x0002);

  runOnce(run1, sizeof(run1), idCmd, idVendor);
  Serial.printf(
      "values cmd=%u tel=%u again=%u vendor=%u overflow=%u got=%d "
      "telemetry=%02X%02X\n",
      idCmd, idTel, idAgain, idVendor, overflowId, gotTelemetry ? 1 : 0,
      telemetry[0], telemetry[1]);
  Serial.print(run1);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);

  runOnce(run2, sizeof(run2), idCmd, idVendor);
  runOnce(run3, sizeof(run3), idCmd, idVendor);
  Serial.printf("run2_same=%d run3_same=%d\n",
                strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);

  Serial.println("TEST done");
}

void loop() { delay(10); }
