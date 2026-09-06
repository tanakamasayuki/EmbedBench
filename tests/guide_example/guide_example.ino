// Built from the code block in docs/GUIDE.md, so the guide cannot go
// stale without a test failing. Keep the two in step.
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <temp_model.h>          // a model from the catalog

static TempSensorModel sensor;

// 1) how the model reaches the outside world. DevicePort routes
//    everything to the environment already; all it needs is which board
//    pin each of the device's own lines corresponds to.
static ebhost::DevicePort port;

// 2) the world's channel reaches the model
static bool onChannel(uint8_t ch, const uint8_t* d, size_t n, void*) {
  return ch == 0 && sensor.channelWrite(TempSensorModel::kChannelTemp, d, n);
}

// 3) wiring the bus to the model
static uint8_t onWrite(const uint8_t* d, size_t n, bool stop, bool cont, void*) {
  const ebdev::I2cTransfer xfer = {stop, cont};
  return sensor.i2cWrite(d, n, xfer);
}
static size_t onRead(uint8_t* d, size_t n, bool stop, bool cont, void*) {
  const ebdev::I2cTransfer xfer = {stop, cont};
  return sensor.i2cRead(d, n, xfer);
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start guide_example");
  Wire.begin(21, 22, 400000);

  port.mapLine(TempSensorModel::kLineDataReady, 27);  // its line is pin 27
  sensor.attach(&port);
  const ebhost::WireDeviceOps ops = {&onWrite, &onRead, nullptr};
  ebhost::bindWireDevice(0x48, ops);  // this model answers at this address
  ebhost::setChannelHandler(&onChannel);
  sensor.reset();

  ebhost::runBegin(1000);                // start recording, 1,000 us tick

  // The world puts a temperature on the sensor. This is what `channel`
  // is for: not a bus and not a pin, but the test moving reality.
  const uint8_t reading[2] = {0x00, 0xFA};
  ebhost::chanWrite(ebhost::Origin::kDir, 0, reading, 2);

  // --- everything below is the code under test, unchanged ---
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(uint16_t(0x48), size_t(2), true);
  const int hi = Wire.read();
  const int lo = Wire.read();
  // ----------------------------------------------------------

  ebhost::runEnd();                      // stop recording

  static char trace[2048];
  ebhost::formatTrace(trace, sizeof(trace));
  Serial.printf("values temp=%02X%02X\n", hi, lo);
  Serial.print(trace);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n",
                s.events, s.dropped, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
