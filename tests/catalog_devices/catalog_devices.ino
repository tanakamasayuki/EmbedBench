// The same two catalog devices on the host core, driven by unmodified
// Arduino application code: Wire for the sensor, Serial1 for the GPS.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostUart.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include <env_sensor_model.h>
#include <gps_model.h>

static EnvSensorModel sensor;
static GpsModel gps;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t line, uint8_t level) override {
    if (line == EnvSensorModel::kLineDataReady) {
      ebd::pinInject(ebd::Origin::kDev, 27, level);
    }
  }
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebd::uartInject(ebd::Origin::kDev, data, len);
  }
  bool requestWake(uint64_t whenUs) override {
    return ebd::requestWake(whenUs);
  }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
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
static void devUartTx(const uint8_t* data, size_t len, void*) {
  gps.serialIn(data, len);
}
static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return sensor.channelWrite(channel, data, len);
}
static void advanceDevices(uint64_t nowUs, void*) {
  sensor.advanceTo(nowUs);
  gps.advanceTo(nowUs);
}
// [adapter end]

// --- Application: ordinary Arduino code ------------------------------------
static uint8_t readRegister(uint8_t reg) {
  Wire.beginTransmission(0x76);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(static_cast<uint16_t>(0x76), static_cast<size_t>(1), true);
  return Wire.available() ? Wire.read() : 0xFF;
}

static uint8_t appChipId = 0;
static uint32_t appPolls = 0;
static uint32_t appRaw = 0;
static char appSentence[24] = {0};

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start catalog_devices");
  Wire.begin(21, 22, 400000);
  Serial1.begin(9600);
  Serial1.setTimeout(20);
  pinMode(27, INPUT);
  sensor.attach(&draftPort);
  gps.attach(&draftPort);
  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x76, ops);
  ebd::bindUartDevice(&devUartTx);
  ebd::setChannelHandler(&devChannel);
  ebd::bindTickDevice(&advanceDevices);
  sensor.reset();
  gps.reset();

  ebd::runBegin(1000);
  const uint8_t temp[3] = {0x7F, 0xE0, 0x00};
  ebd::chanWrite(ebd::Origin::kDir, EnvSensorModel::kChannelTemp, temp, 3);

  // The application identifies the part, starts a measurement, and polls
  // the status register until the part says it is done.
  appChipId = readRegister(EnvSensorModel::kRegChipId);
  Wire.beginTransmission(0x76);
  Wire.write(EnvSensorModel::kRegCtrl);
  Wire.write(EnvSensorModel::kCmdForced);
  Wire.endTransmission();
  while (appPolls < 20 &&
         (readRegister(EnvSensorModel::kRegStatus) &
          EnvSensorModel::kStatusMeasuring) != 0) {
    ++appPolls;
    delay(1);
  }

  Wire.beginTransmission(0x76);
  Wire.write(EnvSensorModel::kRegTemp);
  Wire.endTransmission(false);
  Wire.requestFrom(static_cast<uint16_t>(0x76), static_cast<size_t>(3), true);
  appRaw = 0;
  while (Wire.available()) appRaw = (appRaw << 8) | Wire.read();

  // The GPS runs on its own schedule; the application reads one sentence.
  Serial1.print("START\n");
  const size_t got = Serial1.readBytesUntil('\n', appSentence,
                                            sizeof(appSentence) - 1);
  appSentence[got] = '\0';

  char text[64];
  sensor.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  gps.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();

  static char trace[3072];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values chip=%02X polls=%u raw=%06X ready=%d sentence=%s\n",
                appChipId, appPolls, appRaw, digitalRead(27), appSentence);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
