// Does the shim build for a real board? Compiled for esp32:esp32:esp32 by
// the test, never flashed here. On ESP32 Arduino 3.x the I2C entry points
// are virtual, so handing &cap to a library that takes a TwoWire* records
// its transfers too — which is what the pointer below stands for.
#include <Arduino.h>
#include <CaptureWire.h>
#include <Wire.h>

CaptureWire cap(Wire);
TwoWire* bus = &cap;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  bus->beginTransmission(0x76);
  bus->write(0xD0);
  bus->endTransmission(false);
  bus->requestFrom(static_cast<uint8_t>(0x76), static_cast<size_t>(1), true);
  while (bus->available()) bus->read();
}

void loop() { delay(1000); }
