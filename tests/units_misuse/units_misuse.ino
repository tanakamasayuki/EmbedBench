// The mistakes the sketch itself makes. Every other experiment drives
// the library correctly; this one drives it wrong on purpose, because a
// verification library that fails silently when it is misused is worse
// than no library at all. Each case here has to leave evidence.
#include <Arduino.h>
#include <EmbedBench.h>
#include <SPI.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include <unit_encoder_model.h>

static UnitEncoderModel encoder;

static const uint8_t kAddrEncoder = 0x40;
static const uint8_t kAddrNobody = 0x77;

// [adapter begin]
class MisusePort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
    return true;
  }
};

static MisusePort port;

static uint8_t encWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return encoder.i2cWrite(data, len, xfer);
}
static size_t encRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return encoder.i2cRead(data, len, xfer);
}
static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  if (channel != 0) return false;  // only channel 0 is handled
  return encoder.channelWrite(UnitEncoderModel::kChannelTurn, data, len);
}
// [adapter end]

static uint8_t beforeStatus = 0;
static uint32_t outsideBefore = 0;
static uint32_t outsideAfter = 0;
static uint32_t windowsBefore = 0;
static uint8_t unboundStatus = 0;
static size_t unboundBytes = 0;
static uint8_t staleRead[2] = {0, 0};
static uint8_t noDeviceSpi = 0;
static uint16_t badFormat = 0;
static bool badFrame = false;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_misuse");
  Wire.begin(21, 22, 400000);
  SPI.begin(18, 19, 23, 5);

  encoder.attach(&port);
  const ebd::WireDeviceOps ops = {&encWrite, &encRead, nullptr};
  ebd::bindWireDevice(kAddrEncoder, ops);
  ebd::setChannelHandler(&routeChannel);
  encoder.reset();

  // Mistake 1: work before the run window is open. Bus traffic here is
  // not merely unrecorded, it is invisible — runBegin is what installs
  // the host hooks, so nothing is connected yet and the environment
  // cannot know it happened. A direct sink call it can see, and counts.
  Wire.beginTransmission(kAddrEncoder);
  Wire.write(UnitEncoderModel::kRegCounter);
  beforeStatus = Wire.endTransmission();
  const uint8_t early[1] = {0x01};
  ebd::chanWrite(ebd::Origin::kDir, 0, early, 1);
  windowsBefore = ebd::stats().windows;
  outsideBefore = ebd::stats().outsideWindow;

  ebd::runBegin(1000);

  // Mistake 2: an address nobody is bound to. The host core's Wire has no
  // opinion; the environment says so.
  Wire.beginTransmission(kAddrNobody);
  Wire.write(0x00);
  unboundStatus = Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(kAddrNobody), static_cast<size_t>(2),
                   true);
  unboundBytes = Wire.available();
  while (Wire.available()) Wire.read();

  // Mistake 3: reading without selecting a register first. The bus is
  // fine and the part answers — with whatever register was last set,
  // which is a bug in the sketch that no status code can report.
  Wire.requestFrom(static_cast<uint16_t>(kAddrEncoder),
                   static_cast<size_t>(2), true);
  size_t got = 0;
  while (Wire.available() && got < 2) staleRead[got++] = Wire.read();

  // Mistake 4: a channel nobody handles.
  const uint8_t payload[1] = {0x01};
  ebd::chanWrite(ebd::Origin::kDir, 9, payload, 1);

  // Mistake 5: SPI with no device bound at all.
  noDeviceSpi = SPI.transfer(0x5A);

  // Mistake 6: a frame in a format that was never registered.
  const uint8_t frame[2] = {0x01, 0x02};
  badFrame = ebd::frameTx(ebd::Origin::kApp, 0, 0x1234, frame, 16);
  // And a format name longer than the interface allows.
  badFormat = ebd::registerFormat("vendor.protocol.version.1", 0x1234);

  ebd::runEnd();

  // Mistake 7: work after the window closed, which is the same trap as
  // the first one at the other end of the run.
  const uint8_t early2[1] = {0x01};
  Wire.beginTransmission(kAddrEncoder);
  Wire.write(UnitEncoderModel::kRegCounter);
  Wire.endTransmission();
  ebd::chanWrite(ebd::Origin::kDir, 0, early2, 1);
  outsideAfter = ebd::stats().outsideWindow;

  static char trace[4096];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values before=%u windows0=%u outside=%u,%u unbound=%u,%u\n",
                beforeStatus, windowsBefore, outsideBefore, outsideAfter,
                unboundStatus, static_cast<unsigned>(unboundBytes));
  Serial.printf("values stale=%02X%02X spi=%02X frame=%d fmt=%u\n",
                staleRead[0], staleRead[1], noDeviceSpi, badFrame ? 1 : 0,
                badFormat);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u outside=%u windows=%u\n",
                s.events, s.dropped, s.diagCount, s.outsideWindow, s.windows);
  Serial.println("TEST done");
}

void loop() { delay(10); }
