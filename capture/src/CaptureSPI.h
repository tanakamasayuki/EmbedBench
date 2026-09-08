// CaptureSPI — records SPI transfers the application makes, for taking a
// capture on a real board.
//
// Unlike CaptureWire this is a wrapper, not an SPIClass: SPIClass has no
// virtual entry points on the cores measured (ESP32 Arduino 3.x, the host
// core), so a library holding an SPIClass* cannot be intercepted without
// changing the library. Application code that calls SPI itself can use
// the wrapper where it used SPI. One line per transfer call:
//
//     123456 spi.xfer mosi=05FF miso=FF02
//
// with MOSI and MISO whole, MSB first as on the wire. Chip select is the
// application's digitalWrite and is not seen here (CaptureLines can watch
// a pin, or a logic analyzer has it). devices/tools/trace2tape.py turns
// the lines into kSpi steps, matched byte by byte.
//
//     CaptureSPI cap(SPI);             // lines go to Serial
//     void setup() { Serial.begin(115200); SPI.begin(); }
//     // application code: `cap.transfer(...)` where it used `SPI.transfer`
#pragma once

#include <Arduino.h>
#include <SPI.h>

class CaptureSPI {
 public:
  static const size_t kChunkMax = 64;

  explicit CaptureSPI(SPIClass& inner, Print& out = Serial,
                      const char* prefix = "")
      : inner_(inner), out_(&out), prefix_(prefix) {}

  void setOutput(Print& out) { out_ = &out; }
  SPIClass& inner() { return inner_; }  // for calls this wrapper lacks

  void beginTransaction(SPISettings settings) { inner_.beginTransaction(settings); }
  void endTransaction() { inner_.endTransaction(); }

  uint8_t transfer(uint8_t mosi) {
    const unsigned long at = micros();
    const uint8_t miso = inner_.transfer(mosi);
    emit(at, &mosi, &miso, 1);
    return miso;
  }
  uint16_t transfer16(uint16_t data) {
    const unsigned long at = micros();
    const uint16_t reply = inner_.transfer16(data);
    const uint8_t mosi[2] = {static_cast<uint8_t>(data >> 8),
                             static_cast<uint8_t>(data & 0xFF)};
    const uint8_t miso[2] = {static_cast<uint8_t>(reply >> 8),
                             static_cast<uint8_t>(reply & 0xFF)};
    emit(at, mosi, miso, 2);
    return reply;
  }
  // In place: the buffer holds MOSI going in and MISO coming out.
  void transfer(void* data, size_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    for (size_t pos = 0; pos < size; pos += kChunkMax) {
      const size_t n = size - pos < kChunkMax ? size - pos : kChunkMax;
      uint8_t mosi[kChunkMax];
      for (size_t i = 0; i < n; ++i) mosi[i] = bytes[pos + i];
      const unsigned long at = micros();
      inner_.transfer(bytes + pos, n);
      emit(at, mosi, bytes + pos, n);
    }
  }
  void transferBytes(const uint8_t* data, uint8_t* out, size_t size) {
    for (size_t pos = 0; pos < size; pos += kChunkMax) {
      const size_t n = size - pos < kChunkMax ? size - pos : kChunkMax;
      uint8_t miso[kChunkMax];
      const unsigned long at = micros();
      inner_.transferBytes(data + pos, out != nullptr ? out + pos : miso, n);
      emit(at, data + pos, out != nullptr ? out + pos : miso, n);
    }
  }

 private:
  void emit(unsigned long at, const uint8_t* mosi, const uint8_t* miso,
            size_t n) {
    if (out_ == nullptr) return;
    static const char digits[] = "0123456789ABCDEF";
    char line[kChunkMax * 4 + 48];
    int w = snprintf(line, sizeof(line), "%lu spi.xfer mosi=", at);
    for (size_t i = 0; i < n; ++i) {
      line[w++] = digits[mosi[i] >> 4];
      line[w++] = digits[mosi[i] & 0x0F];
    }
    w += snprintf(line + w, sizeof(line) - w, " miso=");
    for (size_t i = 0; i < n; ++i) {
      line[w++] = digits[miso[i] >> 4];
      line[w++] = digits[miso[i] & 0x0F];
    }
    line[w] = '\0';
    out_->print(prefix_);
    out_->println(line);
  }

  SPIClass& inner_;
  Print* out_;
  const char* prefix_;
};
