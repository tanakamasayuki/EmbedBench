// Two shapes the catalog was missing. A SPI flash whose protocol is
// locked by sequence — a program without its write-enable is discarded,
// and chip select frames the command the way STOP frames an I2C
// transfer. And an audio codec that lives on two buses at once, where
// what the control bus was told decides what the data bus does.
#include <Arduino.h>
#include <EmbedBench.h>
#include <SPI.h>
#include <Wire.h>
#include <EmbedBench.h>
#include <embedbench_draft.h>
#include <string.h>

#include <unit_codec_model.h>
#include <unit_flash_model.h>

static UnitFlashModel flash;
static UnitCodecModel codec;

static const uint8_t kPinFlashCs = 5;
static const uint8_t kPinCodecCs = 15;
static const uint8_t kAddrCodec = 0x1A;

// [adapter begin]
class MixedPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool requestWake(uint64_t whenUs) override { return ebd::requestWake(whenUs); }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
    return true;
  }
};

static MixedPort port;

// The sketch drives chip select with ordinary digitalWrite; the write is
// recorded and then handed to whichever model owns that pin. The adapter
// remembers which one is low rather than reading the pin back for every
// byte, which would put a read in the log per transferred byte.
static ebdev::Device* selected = nullptr;

static void forwardPins(uint8_t pin, uint8_t value, void*) {
  if (pin == kPinFlashCs) {
    flash.lineIn(UnitFlashModel::kLineSelect, value);
    if (value == 0) selected = &flash;
    else if (selected == &flash) selected = nullptr;
  }
  if (pin == kPinCodecCs) {
    codec.lineIn(UnitCodecModel::kLineSelect, value);
    if (value == 0) selected = &codec;
    else if (selected == &codec) selected = nullptr;
  }
}

// One SPI bus with two chip selects: whichever part is selected answers.
static uint8_t spiTransfer(uint8_t mosi, void*) {
  return selected != nullptr ? selected->spiTransfer(mosi) : 0xFF;
}

static uint8_t codecWrite(const uint8_t* data, size_t len, bool stop,
                          bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return codec.i2cWrite(data, len, xfer);
}
static size_t codecRead(uint8_t* data, size_t len, bool stop, bool continued,
                        void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return codec.i2cRead(data, len, xfer);
}
static void advanceMixed(uint64_t nowUs, void*) { flash.advanceTo(nowUs); }
// [adapter end]

// --- Application ------------------------------------------------------------
static void flashCommand(const uint8_t* bytes, size_t len, uint8_t* reply) {
  digitalWrite(kPinFlashCs, LOW);
  for (size_t i = 0; i < len; ++i) {
    const uint8_t got = SPI.transfer(bytes[i]);
    if (reply != nullptr) reply[i] = got;
  }
  digitalWrite(kPinFlashCs, HIGH);
}

static uint8_t statusBeforeWren = 0;
static uint8_t statusAfterWren = 0;
static uint8_t readBackNoWren = 0;
static uint8_t statusDuringProgram = 0;
static uint8_t readBackAfter = 0;
static uint8_t codecCount = 0;
static uint8_t loudSample = 0;
static uint8_t quietSample = 0;
static uint8_t mutedSample = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_mixed");
  SPI.begin(18, 19, 23, kPinFlashCs);
  Wire.begin(21, 22, 400000);
  pinMode(kPinFlashCs, OUTPUT);
  pinMode(kPinCodecCs, OUTPUT);
  digitalWrite(kPinFlashCs, HIGH);
  digitalWrite(kPinCodecCs, HIGH);

  flash.attach(&port);
  codec.attach(&port);
  ebd::bindSpiDevice(&spiTransfer);
  ebd::setPinWriteForward(&forwardPins);
  const ebd::WireDeviceOps codecOps = {&codecWrite, &codecRead, nullptr};
  ebd::bindWireDevice(kAddrCodec, codecOps);
  ebd::bindTickDevice(&advanceMixed);
  flash.reset();
  codec.reset();

  ebd::runBegin(1000);

  // The mistake this part exists to catch: program without enabling
  // writes first. A real chip drops it and says nothing.
  uint8_t status[2] = {0, 0};
  const uint8_t rdsr[2] = {UnitFlashModel::kCmdStatus, 0x00};
  flashCommand(rdsr, 2, status);
  statusBeforeWren = status[1];
  const uint8_t badProgram[4] = {UnitFlashModel::kCmdPageProgram, 0x00, 0xDE,
                                 0xAD};
  flashCommand(badProgram, 4, nullptr);
  uint8_t readback[3] = {0, 0, 0};
  const uint8_t readCmd[3] = {UnitFlashModel::kCmdRead, 0x00, 0x00};
  flashCommand(readCmd, 3, readback);
  readBackNoWren = readback[2];

  // Done properly: enable, program, and the part is busy for 3 ms.
  const uint8_t wren[1] = {UnitFlashModel::kCmdWriteEnable};
  flashCommand(wren, 1, nullptr);
  flashCommand(rdsr, 2, status);
  statusAfterWren = status[1];
  const uint8_t goodProgram[4] = {UnitFlashModel::kCmdPageProgram, 0x00, 0xBE,
                                  0xEF};
  flashCommand(goodProgram, 4, nullptr);
  flashCommand(rdsr, 2, status);
  statusDuringProgram = status[1];
  delay(4);
  flashCommand(readCmd, 3, readback);
  readBackAfter = readback[2];

  // The codec: configuration on I2C, samples on SPI, one part.
  digitalWrite(kPinCodecCs, LOW);
  loudSample = SPI.transfer(0x80);  // full volume
  digitalWrite(kPinCodecCs, HIGH);

  Wire.beginTransmission(kAddrCodec);
  Wire.write(UnitCodecModel::kRegVolume);
  Wire.write(0x40);  // quarter volume
  Wire.endTransmission();
  digitalWrite(kPinCodecCs, LOW);
  quietSample = SPI.transfer(0x80);
  digitalWrite(kPinCodecCs, HIGH);

  Wire.beginTransmission(kAddrCodec);
  Wire.write(UnitCodecModel::kRegMute);
  Wire.write(1);
  Wire.endTransmission();
  digitalWrite(kPinCodecCs, LOW);
  mutedSample = SPI.transfer(0x80);
  digitalWrite(kPinCodecCs, HIGH);

  // The control bus counts what the data bus saw: one part, two paths.
  Wire.beginTransmission(kAddrCodec);
  Wire.write(UnitCodecModel::kRegCount);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(kAddrCodec), static_cast<size_t>(1),
                   true);
  if (Wire.available()) codecCount = Wire.read();

  char text[64];
  flash.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  codec.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();

  static char trace[6144];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values sr0=%02X sr_wren=%02X sr_busy=%02X no_wren=%02X "
                "after=%02X\n",
                statusBeforeWren, statusAfterWren, statusDuringProgram,
                readBackNoWren, readBackAfter);
  Serial.printf("values loud=%02X quiet=%02X muted=%02X count=%u\n",
                loudSample, quietSample, mutedSample, codecCount);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u folded=%u diag=%u\n", s.events,
                s.dropped, s.folded, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
