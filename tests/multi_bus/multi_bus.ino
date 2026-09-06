// Two instances of each bus at once: a register-map device on Wire and
// another on Wire1 at the same address, and a modem on each serial port.
// The interface rule is that one device owns one endpoint on one link, so
// composite hardware is composed from several devices — this is what that
// looks like when the environment actually offers two of each.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostUart.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include <modem_model.h>
#include <regmap_model.h>

static RegisterMapModel sensorA;  // Wire, address 0x50
static RegisterMapModel sensorB;  // Wire1, same address
static AtModemModel modem1;       // Serial1
static AtModemModel modem2;       // Serial2

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  explicit DraftPort(ebd::SerialPort port) : port_(port) {}
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebd::uartInjectOn(ebd::Origin::kDev, port_, data, len);
  }

 private:
  ebd::SerialPort port_;
};

static DraftPort port1(ebd::SerialPort::kSerial1);
static DraftPort port2(ebd::SerialPort::kSerial2);

static uint8_t devWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void* user) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return static_cast<RegisterMapModel*>(user)->i2cWrite(data, len, xfer);
}
static size_t devRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void* user) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return static_cast<RegisterMapModel*>(user)->i2cRead(data, len, xfer);
}
static void uart1Tx(const uint8_t* data, size_t len, void*) {
  modem1.serialIn(data, len);
}
static void uart2Tx(const uint8_t* data, size_t len, void*) {
  modem2.serialIn(data, len);
}
static void advanceDevices(uint64_t nowUs, void*) {
  modem1.advanceTo(nowUs);
  modem2.advanceTo(nowUs);
}
// [adapter end]

static int appA = -1;
static int appB = -1;

static uint8_t readRegister(TwoWire& bus, uint8_t address) {
  bus.beginTransmission(address);
  bus.write(0x01);
  bus.endTransmission(false);  // repeated start, as this model requires
  bus.requestFrom(static_cast<uint16_t>(address), static_cast<size_t>(1),
                  true);
  return bus.available() ? bus.read() : 0xFF;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start multi_bus");
  Wire.begin(21, 22, 400000);
  Wire1.begin(25, 26, 400000);
  Serial1.begin(9600);
  Serial2.begin(9600);
  Serial1.setTimeout(10);
  Serial2.setTimeout(10);

  sensorA.attach(&port1);
  sensorB.attach(&port1);
  modem1.attach(&port1);
  modem2.attach(&port2);
  const ebd::WireDeviceOps opsA = {&devWrite, &devRead, &sensorA};
  const ebd::WireDeviceOps opsB = {&devWrite, &devRead, &sensorB};
  ebd::bindWireDeviceOn(ebd::WireBus::kWire0, 0x50, opsA);
  ebd::bindWireDeviceOn(ebd::WireBus::kWire1, 0x50, opsB);
  ebd::bindUartDeviceOn(ebd::SerialPort::kSerial1, &uart1Tx);
  ebd::bindUartDeviceOn(ebd::SerialPort::kSerial2, &uart2Tx);
  ebd::bindTickDevice(&advanceDevices);

  sensorA.reset();
  sensorB.reset();
  modem1.reset();
  modem2.reset();
  // Give the two sensors different contents so a mix-up would show.
  ebd::runBegin(1000);
  Wire.beginTransmission(0x50);
  Wire.write(0x01);
  Wire.write(0xA1);
  Wire.endTransmission();
  Wire1.beginTransmission(0x50);
  Wire1.write(0x01);
  Wire1.write(0xB2);
  Wire1.endTransmission();

  appA = readRegister(Wire, 0x50);
  appB = readRegister(Wire1, 0x50);

  // Both modems answer their own port, immediately.
  Serial1.print("AT;");
  Serial2.print("AT;");
  uint8_t reply1[2] = {0};
  uint8_t reply2[2] = {0};
  Serial1.readBytes(reply1, sizeof(reply1));
  Serial2.readBytes(reply2, sizeof(reply2));
  ebd::runEnd();

  static char trace[2048];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values a=%02X b=%02X r1=%c%c r2=%c%c\n", appA, appB,
                reply1[0], reply1[1], reply2[0], reply2[1]);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u diag=%u\n", s.events, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
