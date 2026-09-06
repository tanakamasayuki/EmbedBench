// The failures a bench cannot reproduce on demand: a part that is not
// answering, one that drops out for a few transactions and comes back,
// a read that stops short of what was asked for, and what survives a
// power cycle when the part is non-volatile.
#include <Arduino.h>
#include <EmbedBench.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

#include <unit_faulty_model.h>
#include <unit_flash_model.h>

static UnitFaultyModel faulty;
static UnitFlashModel flash;

static const uint8_t kAddrFaulty = 0x20;
static const uint8_t kPinFlashCs = 5;

// [adapter begin]
class FaultPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool requestWake(uint64_t whenUs) override { return ebhost::requestWake(whenUs); }
  bool diagnose(const char* text) override {
    ebhost::deviceNote(text);
    return true;
  }
};

static FaultPort port;

static uint8_t faultyWrite(const uint8_t* data, size_t len, bool stop,
                           bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return faulty.i2cWrite(data, len, xfer);
}
static size_t faultyRead(uint8_t* data, size_t len, bool stop, bool continued,
                         void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return faulty.i2cRead(data, len, xfer);
}
static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  if (channel != 0) return false;
  return faulty.channelWrite(UnitFaultyModel::kChannelFault, data, len);
}
static uint8_t spiTransfer(uint8_t mosi, void*) {
  return flash.spiTransfer(mosi);
}
static void forwardPins(uint8_t pin, uint8_t value, void*) {
  if (pin == kPinFlashCs) flash.lineIn(UnitFlashModel::kLineSelect, value);
}
static void advanceFault(uint64_t nowUs, void*) { flash.advanceTo(nowUs); }
// [adapter end]

// --- Application ------------------------------------------------------------
// An ordinary defensive driver: check the status, count what came back.
static uint8_t probe(uint8_t reg, uint8_t* out, size_t want) {
  Wire.beginTransmission(kAddrFaulty);
  Wire.write(reg);
  const uint8_t status = Wire.endTransmission();
  if (status != 0) return status;
  Wire.requestFrom(static_cast<uint16_t>(kAddrFaulty), want, true);
  size_t got = 0;
  while (Wire.available() && got < want) out[got++] = Wire.read();
  return got == want ? 0 : 0x80 | static_cast<uint8_t>(got);
}

static void setFault(uint8_t mode, uint8_t count) {
  const uint8_t cmd[2] = {mode, count};
  ebhost::chanWrite(ebhost::Origin::kDir, 0, cmd, 2);
}

static void flashCommand(const uint8_t* bytes, size_t len, uint8_t* reply) {
  digitalWrite(kPinFlashCs, LOW);
  for (size_t i = 0; i < len; ++i) {
    const uint8_t got = SPI.transfer(bytes[i]);
    if (reply != nullptr) reply[i] = got;
  }
  digitalWrite(kPinFlashCs, HIGH);
}

static uint8_t healthy = 0xEE;
static uint8_t absent = 0xEE;
static uint8_t refusing = 0xEE;
static uint8_t shortRead = 0xEE;
static uint8_t recovered = 0xEE;
static uint8_t flaky[3] = {0xEE, 0xEE, 0xEE};
static uint8_t beforeCycle = 0;
static uint8_t afterCycle = 0;
static uint8_t afterErase = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_fault");
  Wire.begin(21, 22, 400000);
  SPI.begin(18, 19, 23, kPinFlashCs);
  pinMode(kPinFlashCs, OUTPUT);
  digitalWrite(kPinFlashCs, HIGH);

  faulty.attach(&port);
  flash.attach(&port);
  const ebhost::WireDeviceOps ops = {&faultyWrite, &faultyRead, nullptr};
  ebhost::bindWireDevice(kAddrFaulty, ops);
  ebhost::setChannelHandler(&routeChannel);
  ebhost::bindSpiDevice(&spiTransfer);
  ebhost::setPinWriteForward(&forwardPins);
  ebhost::bindTickDevice(&advanceFault);
  faulty.reset();
  flash.reset();

  ebhost::runBegin(1000);

  uint8_t buf[4] = {0, 0, 0, 0};
  healthy = probe(0x00, buf, 4);

  // Not powered yet: the address is not acknowledged and the driver
  // never gets as far as reading.
  setFault(UnitFaultyModel::kAbsent, 0);
  absent = probe(0x00, buf, 4);

  // Answering but rejecting the payload.
  setFault(UnitFaultyModel::kRefusing, 0);
  refusing = probe(0x00, buf, 4);

  // A read that stops short: the status is fine and the byte count is
  // not, which is the failure a driver is most likely to ignore.
  setFault(UnitFaultyModel::kShortRead, 0);
  shortRead = probe(0x00, buf, 4);

  // Intermittent: two transactions fail, then it comes back by itself
  // with no intervention from the sketch.
  setFault(UnitFaultyModel::kAbsent, 2);
  flaky[0] = probe(0x00, buf, 4);
  flaky[1] = probe(0x00, buf, 4);
  flaky[2] = probe(0x00, buf, 4);
  setFault(UnitFaultyModel::kHealthy, 0);
  recovered = probe(0x00, buf, 4);

  // The power cycle. Program a byte, then reset the part: what a
  // non-volatile device holds is exactly what survives.
  const uint8_t wren[1] = {UnitFlashModel::kCmdWriteEnable};
  const uint8_t program[4] = {UnitFlashModel::kCmdPageProgram, 0x00, 0x5A,
                              0xA5};
  const uint8_t readCmd[3] = {UnitFlashModel::kCmdRead, 0x00, 0x00};
  uint8_t readback[3] = {0, 0, 0};
  flashCommand(wren, 1, nullptr);
  flashCommand(program, 4, nullptr);
  delay(4);
  flashCommand(readCmd, 3, readback);
  beforeCycle = readback[2];

  flash.reset();  // the power cycle
  flashCommand(readCmd, 3, readback);
  afterCycle = readback[2];

  // Blanking it again takes a chip erase, the same as on the bench.
  const uint8_t erase[1] = {UnitFlashModel::kCmdChipErase};
  flashCommand(wren, 1, nullptr);
  flashCommand(erase, 1, nullptr);
  delay(9);
  flashCommand(readCmd, 3, readback);
  afterErase = readback[2];

  char text[64];
  faulty.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  flash.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  ebhost::runEnd();

  static char trace[6144];
  ebhost::formatTrace(trace, sizeof(trace));
  Serial.printf("values ok=%02X absent=%02X refuse=%02X short=%02X back=%02X\n",
                healthy, absent, refusing, shortRead, recovered);
  Serial.printf("values flaky=%02X,%02X,%02X\n", flaky[0], flaky[1], flaky[2]);
  Serial.printf("values cycle=%02X,%02X,%02X\n", beforeCycle, afterCycle,
                afterErase);
  Serial.print(trace);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u folded=%u diag=%u\n", s.events,
                s.dropped, s.folded, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
