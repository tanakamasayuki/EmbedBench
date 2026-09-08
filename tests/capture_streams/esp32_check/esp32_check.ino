// Do the four shims build for a real board? Compiled for esp32:esp32:esp32
// by the test, never flashed here.
#include <Arduino.h>
#include <CaptureLines.h>
#include <CaptureSPI.h>
#include <CaptureSerial.h>
#include <CaptureWire.h>
#include <SPI.h>
#include <Wire.h>

CaptureWire capWire(Wire);
CaptureSerial capSerial(Serial1);
CaptureSPI capSpi(SPI);
CaptureLines lines;
Stream* gps = &capSerial;  // what a GPS library would be handed

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
  Wire.begin();
  SPI.begin();
  pinMode(27, INPUT);
  lines.watch(27);
  gps->print("$PMTK000*32\r\n");
  capSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  capSpi.transfer(0x9F);
  uint8_t id[3] = {0, 0, 0};
  capSpi.transfer(id, sizeof(id));
  capSpi.endTransaction();
  capWire.beginTransmission(0x76);
  capWire.write(0xD0);
  capWire.endTransmission(false);
  capWire.requestFrom(static_cast<uint8_t>(0x76), static_cast<size_t>(1), true);
}

void loop() {
  lines.poll();
  while (gps->available()) gps->read();
  delay(1);
}
