// The other three shims on the host: CaptureSerial on the modem's port,
// CaptureSPI on the flash, and CaptureLines watching the sensor's DRDY
// while CaptureWire carries the bus. The host environment records the
// same session underneath, so both views of it exist at once.
#include <Arduino.h>
#include <CaptureLines.h>
#include <CaptureSPI.h>
#include <CaptureSerial.h>
#include <CaptureWire.h>
#include <EmbedBench.h>
#include <HostUart.h>
#include <SPI.h>
#include <Wire.h>

#include <env_sensor_model.h>
#include <modem_model.h>
#include <unit_flash_model.h>

static EnvSensorModel sensor;
static AtModemModel modem;
static UnitFlashModel flash;

static CaptureWire capWire(Wire, Serial, "CAP ");
static CaptureSerial capSerial(Serial1, Serial, "CAP ");
static CaptureSPI capSpi(SPI, Serial, "CAP ");
static CaptureLines lines(Serial, "CAP ");

static const uint8_t kPinDrdy = 27;
static const uint8_t kPinFlashCs = 5;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebhost::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    ebhost::pinInject(ebhost::Origin::kDev, kPinDrdy, level);
  }
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebhost::uartInject(ebhost::Origin::kDev, data, len);
  }
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
static void devUartTx(const uint8_t* data, size_t len, void*) {
  modem.serialIn(data, len);
}
static uint8_t devSpi(uint8_t mosi, void*) { return flash.spiTransfer(mosi); }
static void forwardPins(uint8_t pin, uint8_t value, void*) {
  if (pin == kPinFlashCs) flash.lineIn(UnitFlashModel::kLineSelect, value);
}
static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return sensor.channelWrite(channel, data, len);
}
static void advanceDevices(uint64_t nowUs, void*) {
  sensor.advanceTo(nowUs);
  modem.advanceTo(nowUs);
  flash.advanceTo(nowUs);
}
// [adapter end]

// --- Application, through the shims ------------------------------------------
static uint8_t readRegister(uint8_t reg) {
  capWire.beginTransmission(0x76);
  capWire.write(reg);
  capWire.endTransmission(false);
  capWire.requestFrom(static_cast<uint8_t>(0x76), static_cast<size_t>(1), true);
  return capWire.available() ? static_cast<uint8_t>(capWire.read()) : 0xFF;
}

static uint8_t flashStatus() {
  digitalWrite(kPinFlashCs, LOW);
  capSpi.transfer(UnitFlashModel::kCmdStatus);
  const uint8_t status = capSpi.transfer(0x00);
  digitalWrite(kPinFlashCs, HIGH);
  return status;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start capture_streams");
  Wire.begin(21, 22, 400000);
  Serial1.begin(9600);
  SPI.begin();
  pinMode(kPinDrdy, INPUT);
  pinMode(kPinFlashCs, OUTPUT);
  digitalWrite(kPinFlashCs, HIGH);
  sensor.attach(&draftPort);
  modem.attach(&draftPort);
  flash.attach(&draftPort);
  const ebhost::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebhost::bindWireDevice(0x76, ops);
  ebhost::bindUartDevice(&devUartTx);
  ebhost::bindSpiDevice(&devSpi);
  ebhost::setPinWriteForward(&forwardPins);
  ebhost::setChannelHandler(&devChannel);
  ebhost::bindTickDevice(&advanceDevices);
  sensor.reset();
  modem.reset();
  flash.reset();
  lines.watch(kPinDrdy);

  ebhost::runBegin(1000);
  const uint8_t temp[3] = {0x7F, 0xE0, 0x00};
  ebhost::chanWrite(ebhost::Origin::kDir, EnvSensorModel::kChannelTemp, temp, 3);

  Serial.println("CAPTURE BEGIN");
  // Serial: a command, answered a tick later; the reply is recorded when
  // the application first looks for it.
  capSerial.print("AT+S;");
  while (capSerial.available() < 2) delay(1);
  char reply[3] = {0, 0, 0};
  reply[0] = static_cast<char>(capSerial.read());
  reply[1] = static_cast<char>(capSerial.read());

  // SPI: status, write enable, status again.
  const uint8_t status0 = flashStatus();
  digitalWrite(kPinFlashCs, LOW);
  capSpi.transfer(UnitFlashModel::kCmdWriteEnable);
  digitalWrite(kPinFlashCs, HIGH);
  const uint8_t status1 = flashStatus();

  // I2C plus the line: start a measurement, poll until done while
  // watching DRDY, then read the result.
  capWire.beginTransmission(0x76);
  capWire.write(EnvSensorModel::kRegCtrl);
  capWire.write(EnvSensorModel::kCmdForced);
  capWire.endTransmission();
  uint8_t polls = 0;
  while (polls < 20) {
    lines.poll();
    if ((readRegister(EnvSensorModel::kRegStatus) &
         EnvSensorModel::kStatusMeasuring) == 0) {
      break;
    }
    ++polls;
    delay(1);
  }
  lines.poll();
  capWire.beginTransmission(0x76);
  capWire.write(EnvSensorModel::kRegTemp);
  capWire.endTransmission(false);
  capWire.requestFrom(static_cast<uint8_t>(0x76), static_cast<size_t>(3), true);
  uint32_t raw = 0;
  while (capWire.available()) raw = (raw << 8) | static_cast<uint8_t>(capWire.read());
  Serial.println("CAPTURE END");
  ebhost::runEnd();

  Serial.printf("values reply=%s status0=%02X status1=%02X polls=%u raw=%06X "
                "drdy=%d\n",
                reply, status0, status1, polls, raw, digitalRead(kPinDrdy));
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
