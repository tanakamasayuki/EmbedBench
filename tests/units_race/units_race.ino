// What happens when devices act at the same instant. Two questions the
// catalog had not asked: whether several parts sharing one signal line
// combine correctly, and whether effects landing on the very same
// microsecond come out in a stable order run after run.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostUart.h>
#include <string.h>

#include <unit_ir_model.h>
#include <unit_modbus_model.h>
#include <unit_pir_model.h>

// Two motion sensors wired to one input, the way a real installation
// puts several detectors on one alarm line.
static UnitPirModel pirEarly;
static UnitPirModel pirLate;
// Two more parts, so that three different kinds of effect can be made to
// land on the same microsecond.
static UnitIrModel ir;
static UnitModbusModel modbus;

static const uint8_t kPinAlarm = 26;

// [adapter begin]
// A shared line is the adapter's problem, not the interface's: each
// device is told about its own line and knows nothing of the others, so
// something has to combine them. Doing it naively — passing each
// device's level straight through — lets whichever part releases first
// pull the line down under the one still asserting.
static bool asserted[2] = {false, false};
static uint8_t naiveLevel = 0;   // what a pass-through adapter would show
static uint8_t combinedLevel = 0;

static void setLine(size_t which, uint8_t level) {
  asserted[which] = level != 0;
  naiveLevel = level;
  const uint8_t want = (asserted[0] || asserted[1]) ? 1 : 0;
  if (want == combinedLevel) return;
  combinedLevel = want;
  ebhost::pinInject(ebhost::Origin::kDev, kPinAlarm, want);
}

class PirPort : public ebdev::HostPort {
 public:
  explicit PirPort(size_t which) : which_(which) {}
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override { setLine(which_, level); }
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool requestWake(uint64_t whenUs) override { return ebhost::requestWake(whenUs); }

 private:
  size_t which_;
};

class BusPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebhost::uartInject(ebhost::Origin::kDev, data, len);
  }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebhost::frameRx(ebhost::Origin::kDev, bus, format, data, bits);
  }
  uint16_t formatId(const char* name, uint32_t schema) override {
    return ebhost::registerFormat(name, schema);
  }
  bool requestWake(uint64_t whenUs) override { return ebhost::requestWake(whenUs); }
  bool diagnose(const char* text) override {
    ebhost::deviceNote(text);
    return true;
  }
};

static PirPort earlyPort(0);
static PirPort latePort(1);
static BusPort busPort;

// The order effects come out in is this list's order, not something the
// environment decides. A fixed order is what makes the run reproducible;
// an adapter that walked a hash table or sorted by pointer would not be.
static void advanceAll(uint64_t nowUs, void*) {
  pirEarly.advanceTo(nowUs);
  ir.advanceTo(nowUs);
  modbus.advanceTo(nowUs);
  pirLate.advanceTo(nowUs);
}

static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  switch (channel) {
    case 0: return pirEarly.channelWrite(UnitPirModel::kChannelMotion, data, len);
    case 1: return pirLate.channelWrite(UnitPirModel::kChannelMotion, data, len);
    case 2: return ir.channelWrite(UnitIrModel::kChannelPress, data, len);
    case 3: return modbus.channelWrite(UnitModbusModel::kChannelRegister, data, len);
    default: return false;
  }
}
static void modbusTx(const uint8_t* data, size_t len, void*) {
  modbus.serialIn(data, len);
}
static void appFrame(uint8_t, uint16_t, const uint8_t*, size_t, void*) {}
// [adapter end]

// --- One run ----------------------------------------------------------------
static int lineAt2400 = -1;
static int lineAt2600 = -1;
static int lineAt3600 = -1;
static uint8_t naiveAt2600 = 0;
// How long a 200 us wait that spans a wake actually took.
static uint32_t askedUs = 0;
static uint32_t tookUs = 0;

static void runOnce(char* out, size_t cap) {
  pirEarly.reset();
  pirLate.reset();
  ir.reset();
  modbus.reset();
  asserted[0] = false;
  asserted[1] = false;
  naiveLevel = 0;
  combinedLevel = 0;

  ebhost::runBegin(1000);

  // Both sensors see motion, one a little after the other, so their hold
  // windows overlap: the early one releases at 2500 and the late one at
  // 3500, and the line must stay up throughout.
  const uint8_t motion[1] = {1};
  ebhost::chanWrite(ebhost::Origin::kDir, 0, motion, 1);

  // Three different kinds of effect, all arranged to land on 2500 us:
  // the early sensor's release, an IR repeat 2000 us after its press,
  // and a Modbus reply 1500 us of silence after its request.
  const uint8_t press[3] = {0x40, 0x12, 0x01};
  const uint8_t reg[3] = {0x00, 0xC0, 0xDE};
  ebhost::chanWrite(ebhost::Origin::kDir, 3, reg, 3);
  delayMicroseconds(500);
  ebhost::chanWrite(ebhost::Origin::kDir, 2, press, 3);  // repeat due at 2500
  uint8_t request[8] = {UnitModbusModel::kAddress,
                        UnitModbusModel::kFuncReadHolding,
                        0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
  const uint16_t crc = UnitModbusModel::crc16(request, 6);
  request[6] = static_cast<uint8_t>(crc & 0xFF);
  request[7] = static_cast<uint8_t>(crc >> 8);
  delayMicroseconds(500);
  Serial1.write(request, sizeof(request));  // reply due at 2500
  ebhost::chanWrite(ebhost::Origin::kDir, 1, motion, 1);  // releases at 3500

  delayMicroseconds(1400);
  lineAt2400 = digitalRead(kPinAlarm);
  // This wait spans the 2500 us wake. delayMicroseconds is the one
  // waiter in the core that does not loop to its deadline, so the
  // splitter's early return at a wake cuts it short. Measured here on
  // purpose rather than left to be discovered.
  const uint32_t before = micros();
  askedUs = 200;
  delayMicroseconds(askedUs);
  tookUs = micros() - before;
  lineAt2600 = digitalRead(kPinAlarm);
  naiveAt2600 = naiveLevel;
  delayMicroseconds(1000);
  lineAt3600 = digitalRead(kPinAlarm);

  ebhost::runEnd();
  ebhost::formatTrace(out, cap);
}

static char run1[4096];
static char run2[4096];
static char run3[4096];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_race");
  Serial1.begin(9600);
  pinMode(kPinAlarm, INPUT);

  pirEarly.attach(&earlyPort);
  pirLate.attach(&latePort);
  ir.attach(&busPort);
  modbus.attach(&busPort);
  ebhost::setChannelHandler(&routeChannel);
  ebhost::bindTickDevice(&advanceAll);
  ebhost::bindUartDevice(&modbusTx);
  ebhost::setFrameReceiver(&appFrame);

  runOnce(run1, sizeof(run1));
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("values at2400=%d at2600=%d at3600=%d naive=%u\n", lineAt2400,
                lineAt2600, lineAt3600, naiveAt2600);
  Serial.printf("values asked=%u took=%u\n", askedUs, tookUs);
  Serial.print(run1);
  Serial.printf("stats events=%u dropped=%u folded=%u diag=%u\n", s.events,
                s.dropped, s.folded, s.diagCount);

  runOnce(run2, sizeof(run2));
  runOnce(run3, sizeof(run3));
  Serial.printf("run2_same=%d run3_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
