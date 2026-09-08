// CaptureWire — a TwoWire that records what the application does on the
// bus, for taking a capture on a real board.
//
// It wraps the real bus instance and prints one line per transfer in the
// event grammar EmbedBench's tools read, with the board's micros() first:
//
//     123456 i2c.req addr=76 data=F425 stop=1
//     123500 i2c.resp status=0
//     130000 i2c.rd.req addr=76 req=1 stop=1 rs
//     130010 i2c.rd.resp len=1 data=08
//
// Feed the lines to devices/tools/trace2regtable.py (a register-table
// scaffold) or devices/tools/trace2tape.py (a replayable tape). Unlike the
// host trace, the whole payload is printed: a capture has to carry every
// byte (X63), so kPayloadMax is the only cut and it is announced.
//
// It IS a TwoWire, so a library that takes a TwoWire* can be handed the
// capture. On ESP32 Arduino 3.x the I2C entry points are virtual
// (HardwareI2C) and such a library's calls land here; on cores where they
// are not (the host core), only calls made on the CaptureWire itself are
// recorded, which is all a host-side check of the output needs.
//
//     CaptureWire cap(Wire);           // lines go to Serial
//     void setup() { Serial.begin(115200); Wire.begin(); }
//     // application code: `cap` where it used `Wire`
//
// Not recorded: the lines a part drives (IRQ, DRDY) — a bus-side capture
// cannot see them, so the scaffold's TODO list will not mention them
// either; and the time spent printing, which is why the timestamp is
// taken before the line is formatted.
#pragma once

#include <Arduino.h>
#include <Wire.h>

class CaptureWire : public TwoWire {
 public:
  static const size_t kPayloadMax = 64;

  // `prefix` is printed in front of every line, to tell the capture apart
  // from whatever else the sketch prints on the same port.
  explicit CaptureWire(TwoWire& inner, Print& out = Serial,
                       const char* prefix = "")
      : TwoWire(kDeadBus), inner_(inner), out_(&out), prefix_(prefix) {}

  void setOutput(Print& out) { out_ = &out; }
  uint32_t transfers() const { return transfers_; }

  // --- I2C entry points: the HardwareI2C set --------------------------------
  void beginTransmission(uint8_t address) {
    address_ = address;
    txLen_ = 0;
    txTotal_ = 0;
    inner_.beginTransmission(address);
  }
  uint8_t endTransmission(bool sendStop) {
    const unsigned long at = micros();
    const uint8_t status = inner_.endTransmission(sendStop);
    const bool continued = open_ == address_;
    char hex[kPayloadMax * 2 + 1];
    toHex(tx_, txLen_, hex);
    char line[kPayloadMax * 2 + 64];
    snprintf(line, sizeof(line), "%lu i2c.req addr=%02X data=%s stop=%u%s%s",
             at, address_, hex, sendStop ? 1 : 0, continued ? " rs" : "",
             txTotal_ > txLen_ ? " truncated" : "");
    emit(line);
    snprintf(line, sizeof(line), "%lu i2c.resp status=%u", at, status);
    emit(line);
    open_ = sendStop ? kNoAddress : address_;
    ++transfers_;
    return status;
  }
  uint8_t endTransmission() { return endTransmission(true); }

  size_t requestFrom(uint8_t address, size_t len, bool sendStop) {
    const unsigned long at = micros();
    // uint16_t picks one overload on every core: the host core has
    // several requestFrom() signatures, ESP32 has one.
    const size_t got =
        inner_.requestFrom(static_cast<uint16_t>(address), len, sendStop);
    const bool continued = open_ == address;
    rxLen_ = 0;
    rxPos_ = 0;
    while (rxLen_ < got && rxLen_ < kPayloadMax) {
      const int b = inner_.read();
      if (b < 0) break;
      rx_[rxLen_++] = static_cast<uint8_t>(b);
    }
    char hex[kPayloadMax * 2 + 1];
    toHex(rx_, rxLen_, hex);
    char line[kPayloadMax * 2 + 64];
    snprintf(line, sizeof(line), "%lu i2c.rd.req addr=%02X req=%u stop=%u%s",
             at, address, static_cast<unsigned>(len), sendStop ? 1 : 0,
             continued ? " rs" : "");
    emit(line);
    snprintf(line, sizeof(line), "%lu i2c.rd.resp len=%u data=%s%s", at,
             static_cast<unsigned>(got), hex,
             got > rxLen_ ? " truncated" : "");
    emit(line);
    open_ = sendStop ? kNoAddress : address;
    ++transfers_;
    return got;
  }
  size_t requestFrom(uint8_t address, size_t len) {
    return requestFrom(address, len, true);
  }

  // --- Stream: writes are copied for the record, reads come from our copy --
  size_t write(uint8_t b) override {
    if (txLen_ < kPayloadMax) tx_[txLen_++] = b;
    ++txTotal_;
    return inner_.write(b);
  }
  size_t write(const uint8_t* data, size_t len) override {
    for (size_t i = 0; i < len; ++i) {
      if (txLen_ < kPayloadMax) tx_[txLen_++] = data[i];
    }
    txTotal_ += len;
    return inner_.write(data, len);
  }
  using Print::write;
  int available() override { return static_cast<int>(rxLen_ - rxPos_); }
  int read() override { return rxPos_ < rxLen_ ? rx_[rxPos_++] : -1; }
  int peek() override { return rxPos_ < rxLen_ ? rx_[rxPos_] : -1; }
  void flush() override { inner_.flush(); }

 private:
  // The base object is never begun: it only makes this a TwoWire. A bus
  // number no chip has keeps its destructor away from the real bus.
  static const uint8_t kDeadBus = 0xFE;
  static const uint16_t kNoAddress = 0xFFFF;

  static void toHex(const uint8_t* data, size_t len, char* out) {
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i) {
      out[i * 2] = digits[data[i] >> 4];
      out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
  }
  void emit(const char* line) {
    if (out_ == nullptr) return;
    out_->print(prefix_);
    out_->println(line);
  }

  TwoWire& inner_;
  Print* out_;
  const char* prefix_;
  uint8_t address_ = 0;
  uint16_t open_ = kNoAddress;
  uint8_t tx_[kPayloadMax];
  size_t txLen_ = 0;
  size_t txTotal_ = 0;
  uint8_t rx_[kPayloadMax];
  size_t rxLen_ = 0;
  size_t rxPos_ = 0;
  uint32_t transfers_ = 0;
};
