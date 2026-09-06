// The other unit shapes on the host core: two analog units read with
// analogRead, an I2C encoder read with Wire, and a Modbus RTU slave on a
// serial port whose framing is silence rather than a terminator.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostUart.h>
#include <Wire.h>
#include <string.h>

#include <unit_angle_model.h>
#include <unit_encoder_model.h>
#include <unit_light_model.h>
#include <unit_modbus_model.h>

static UnitAngleModel angle;
static UnitLightModel light;
static UnitEncoderModel encoder;
static UnitModbusModel modbus;

static const uint8_t kPinAngle = 35;
static const uint8_t kPinLight = 36;
static const uint8_t kPinDark = 34;

// [adapter begin]
// Four devices, four ports, no port class written by hand: DevicePort
// already routes to the environment, so only the line-to-pin mapping is
// left. What used to be 38 lines here is now four declarations and the
// mappings in setup().
static ebhost::DevicePort anglePort;
static ebhost::DevicePort lightPort;
static ebhost::DevicePort busPort;

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
static void modbusTx(const uint8_t* data, size_t len, void*) {
  modbus.serialIn(data, len);
}
static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  switch (channel) {
    case 0:
      return angle.channelWrite(UnitAngleModel::kChannelAngle, data, len);
    case 1:
      return light.channelWrite(UnitLightModel::kChannelBrightness, data, len);
    case 2:
      return encoder.channelWrite(UnitEncoderModel::kChannelTurn, data, len);
    case 3:
      return modbus.channelWrite(UnitModbusModel::kChannelRegister, data, len);
    default:
      return false;
  }
}
static void advanceUnits(uint64_t nowUs, void*) { modbus.advanceTo(nowUs); }
// [adapter end]

static uint16_t appAngleRaw = 0;
static uint32_t appAngleMv = 0;
static uint16_t appLight = 0;
static int appDark = -1;
static int16_t appCount = 0;
static uint16_t appRegister = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_bus");
  Wire.begin(21, 22, 400000);
  Serial1.begin(9600);
  Serial1.setTimeout(20);
  pinMode(kPinDark, INPUT);

  anglePort.mapAnalog(0, kPinAngle);
  lightPort.mapAnalog(0, kPinLight);
  lightPort.mapLine(UnitLightModel::kLineDigital, kPinDark);
  angle.attach(&anglePort);
  light.attach(&lightPort);
  encoder.attach(&busPort);
  modbus.attach(&busPort);
  const ebhost::WireDeviceOps ops = {&encWrite, &encRead, nullptr};
  ebhost::bindWireDevice(0x40, ops);
  ebhost::bindUartDevice(&modbusTx);
  ebhost::setChannelHandler(&routeChannel);
  ebhost::bindTickDevice(&advanceUnits);
  angle.reset();
  light.reset();
  encoder.reset();
  modbus.reset();

  ebhost::runBegin(1000);

  // Analog units: the knob is turned and the room goes dark.
  const uint8_t position[2] = {0x08, 0x00};  // 2048 of 4095
  ebhost::chanWrite(ebhost::Origin::kDir, 0, position, 2);
  appAngleRaw = analogRead(kPinAngle);
  appAngleMv = analogReadMilliVolts(kPinAngle);
  const uint8_t dim[2] = {0x01, 0xF4};  // 500, below the 2000 threshold
  ebhost::chanWrite(ebhost::Origin::kDir, 1, dim, 2);
  appLight = analogRead(kPinLight);
  appDark = digitalRead(kPinDark);

  // I2C encoder: the knob turns three detents, the sketch reads the
  // counter and lights the LED.
  const uint8_t turn[1] = {0x03};
  ebhost::chanWrite(ebhost::Origin::kDir, 2, turn, 1);
  Wire.beginTransmission(0x40);
  Wire.write(UnitEncoderModel::kRegCounter);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(0x40), static_cast<size_t>(2), true);
  uint16_t raw = 0;
  if (Wire.available() >= 2) {
    raw = static_cast<uint16_t>(Wire.read());
    raw |= static_cast<uint16_t>(Wire.read()) << 8;
  }
  appCount = static_cast<int16_t>(raw);
  Wire.beginTransmission(0x40);
  Wire.write(UnitEncoderModel::kRegLed);
  Wire.write(0x10);
  Wire.write(0x20);
  Wire.write(0x30);
  Wire.endTransmission();

  // Modbus: read one holding register, then send a frame with a broken
  // checksum, which a real slave answers with silence.
  const uint8_t reg[3] = {0x00, 0xBE, 0xEF};
  ebhost::chanWrite(ebhost::Origin::kDir, 3, reg, 3);
  uint8_t request[8] = {UnitModbusModel::kAddress,
                        UnitModbusModel::kFuncReadHolding,
                        0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
  const uint16_t crc = UnitModbusModel::crc16(request, 6);
  request[6] = static_cast<uint8_t>(crc & 0xFF);
  request[7] = static_cast<uint8_t>(crc >> 8);
  Serial1.write(request, sizeof(request));
  uint8_t reply[8] = {0};
  const size_t got = Serial1.readBytes(reply, 7);
  if (got >= 5) {
    appRegister = static_cast<uint16_t>((reply[3] << 8) | reply[4]);
  }
  request[7] ^= 0xFF;  // corrupt the checksum
  Serial1.write(request, sizeof(request));
  delay(3);

  char text[64];
  angle.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  encoder.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  modbus.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  ebhost::runEnd();

  static char trace[3072];
  ebhost::formatTrace(trace, sizeof(trace));
  Serial.printf("values angle=%u mv=%u light=%u dark=%d count=%d reg=%04X\n",
                appAngleRaw, appAngleMv, appLight, appDark, appCount,
                appRegister);
  Serial.print(trace);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
