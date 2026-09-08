// The board-side capture shim, checked on the host: the application talks
// to the environmental sensor through CaptureWire instead of Wire, and the
// shim prints one line per transfer with the whole payload. The host
// environment records the same run underneath, so the two views of one
// session can be compared, and the shim's lines are what a board would
// have printed.
#include <Arduino.h>
#include <CaptureWire.h>
#include <EmbedBench.h>
#include <Wire.h>

#include <env_sensor_model.h>

static EnvSensorModel sensor;
static CaptureWire cap(Wire, Serial, "CAP ");

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    ebhost::pinInject(ebhost::Origin::kDev, 27, level);
  }
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool requestWake(uint64_t whenUs) override {
    return ebhost::requestWake(whenUs);
  }
  bool diagnose(const char* text) override {
    ebhost::deviceNote(text);
    return true;
  }
};
static DraftPort draftPort;

static uint8_t devWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return sensor.i2cWrite(data, len, xfer);
}
static size_t devRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return sensor.i2cRead(data, len, xfer);
}
static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return sensor.channelWrite(channel, data, len);
}
static void advanceDevices(uint64_t nowUs, void*) { sensor.advanceTo(nowUs); }
// [adapter end]

// --- Application: the catalog_devices sequence, through the shim -----------
static uint8_t readRegister(uint8_t reg, uint8_t* out, size_t n) {
  cap.beginTransmission(0x76);
  cap.write(reg);
  cap.endTransmission(false);
  cap.requestFrom(static_cast<uint8_t>(0x76), n, true);
  uint8_t got = 0;
  while (cap.available() && got < n) out[got++] = static_cast<uint8_t>(cap.read());
  return got;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start capture_shim");
  Wire.begin(21, 22, 400000);
  pinMode(27, INPUT);
  sensor.attach(&draftPort);
  const ebhost::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebhost::bindWireDevice(0x76, ops);
  ebhost::setChannelHandler(&devChannel);
  ebhost::bindTickDevice(&advanceDevices);
  sensor.reset();

  ebhost::runBegin(1000);
  const uint8_t temp[3] = {0x7F, 0xE0, 0x00};
  ebhost::chanWrite(ebhost::Origin::kDir, EnvSensorModel::kChannelTemp, temp, 3);

  Serial.println("CAPTURE BEGIN");
  uint8_t chipId[1] = {0};
  readRegister(EnvSensorModel::kRegChipId, chipId, 1);
  cap.beginTransmission(0x76);
  cap.write(EnvSensorModel::kRegCtrl);
  cap.write(EnvSensorModel::kCmdForced);
  cap.endTransmission();
  delay(1);
  uint8_t early[1] = {0};
  readRegister(EnvSensorModel::kRegStatus, early, 1);
  delay(7);
  uint8_t late[1] = {0};
  readRegister(EnvSensorModel::kRegStatus, late, 1);
  uint8_t reading[3] = {0, 0, 0};
  const uint8_t readLen = readRegister(EnvSensorModel::kRegTemp, reading, 3);
  uint8_t nothing[1] = {0};
  const uint8_t badLen = readRegister(0x42, nothing, 1);
  Serial.println("CAPTURE END");

  char text[64];
  sensor.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  ebhost::runEnd();

  static char trace[3072];
  ebhost::formatTrace(trace, sizeof(trace));
  Serial.printf("values chip=%02X early=%02X late=%02X read_len=%u "
                "data=%02X%02X%02X bad_len=%u transfers=%u\n",
                chipId[0], early[0], late[0], readLen, reading[0], reading[1],
                reading[2], badLen, cap.transfers());
  Serial.print(trace);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
