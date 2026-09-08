// CaptureSerial — a Stream that records both directions of a serial link,
// for taking a capture on a real board.
//
// It wraps the port a device hangs off (Serial1, a SoftwareSerial, any
// Stream) and prints what the application sent and what the device
// answered, in the lines devices/tools/trace2tape.py reads:
//
//     123456 uart.tx AT+S;
//     124500 dev.tx OK
//     130000 uart.tx data=010203FF
//
// Text stays text (with \r \n \t escaped); anything else is hex, whole.
//
// It IS a Stream, so a library that takes a Stream* (most GPS and modem
// libraries do) can be handed the capture; read(), available() and
// peek() are virtual on every core. The device's bytes are recorded when
// the application first looks for them, so a dev.tx timestamp is when
// they were noticed, not when they arrived — an upper bound. Poll
// available() from loop() to keep the bound tight.
//
//     CaptureSerial cap(Serial1);      // lines go to Serial
//     void setup() { Serial.begin(115200); Serial1.begin(9600); }
//     // application code: `cap` where it used `Serial1`
//
// Do not capture the port the lines are printed on.
#pragma once

#include <Arduino.h>

class CaptureSerial : public Stream {
 public:
  static const size_t kChunkMax = 64;
  static const size_t kBufferSize = 256;

  explicit CaptureSerial(Stream& inner, Print& out = Serial,
                         const char* prefix = "")
      : inner_(inner), out_(&out), prefix_(prefix) {}

  void setOutput(Print& out) { out_ = &out; }

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    const unsigned long at = micros();
    const size_t accepted = inner_.write(data, len);
    emit(at, "uart.tx", data, len);
    return accepted;
  }
  using Print::write;

  int available() override {
    drain();
    return static_cast<int>(rxLen_ - rxPos_);
  }
  int read() override {
    drain();
    return rxPos_ < rxLen_ ? rx_[rxPos_++] : -1;
  }
  int peek() override {
    drain();
    return rxPos_ < rxLen_ ? rx_[rxPos_] : -1;
  }
  void flush() override { inner_.flush(); }

 private:
  // Pull what the device has sent so far into our buffer and record it,
  // stamped now. What does not fit stays in the port for the next call.
  void drain() {
    if (rxPos_ == rxLen_) {
      rxPos_ = 0;
      rxLen_ = 0;
    }
    const unsigned long at = micros();
    const size_t start = rxLen_;
    while (rxLen_ < kBufferSize && inner_.available() > 0) {
      const int b = inner_.read();
      if (b < 0) break;
      rx_[rxLen_++] = static_cast<uint8_t>(b);
    }
    if (rxLen_ > start) emit(at, "dev.tx", rx_ + start, rxLen_ - start);
  }

  static bool printable(uint8_t b) {
    return (b >= 0x20 && b <= 0x7E && b != '\\') || b == '\r' || b == '\n' ||
           b == '\t';
  }

  // One line per chunk of at most kChunkMax bytes: a tape matches byte by
  // byte, so where a line breaks carries no meaning.
  void emit(unsigned long at, const char* kind, const uint8_t* data,
            size_t len) {
    if (out_ == nullptr) return;
    if (len == 0) {
      char line[40];
      snprintf(line, sizeof(line), "%lu %s empty", at, kind);
      out_->print(prefix_);
      out_->println(line);
      return;
    }
    for (size_t pos = 0; pos < len; pos += kChunkMax) {
      const size_t n = len - pos < kChunkMax ? len - pos : kChunkMax;
      bool text = true;
      for (size_t i = 0; i < n && text; ++i) text = printable(data[pos + i]);
      char label[kChunkMax * 2 + 8];
      if (text) {
        size_t w = 0;
        for (size_t i = 0; i < n; ++i) {
          const uint8_t b = data[pos + i];
          if (b == '\r' || b == '\n' || b == '\t') {
            label[w++] = '\\';
            label[w++] = b == '\r' ? 'r' : (b == '\n' ? 'n' : 't');
          } else {
            label[w++] = static_cast<char>(b);
          }
        }
        label[w] = '\0';
      } else {
        static const char digits[] = "0123456789ABCDEF";
        label[0] = 'd'; label[1] = 'a'; label[2] = 't'; label[3] = 'a'; label[4] = '=';
        for (size_t i = 0; i < n; ++i) {
          label[5 + i * 2] = digits[data[pos + i] >> 4];
          label[6 + i * 2] = digits[data[pos + i] & 0x0F];
        }
        label[5 + n * 2] = '\0';
      }
      char line[kChunkMax * 2 + 40];
      snprintf(line, sizeof(line), "%lu %s %s", at, kind, label);
      out_->print(prefix_);
      out_->println(line);
    }
  }

  Stream& inner_;
  Print* out_;
  const char* prefix_;
  uint8_t rx_[kBufferSize];
  size_t rxLen_ = 0;
  size_t rxPos_ = 0;
};
