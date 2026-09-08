// CaptureLines — records the lines a part drives (IRQ, DRDY, BUSY), the
// one thing a bus-side capture cannot see (X64).
//
// The application registers the pins it wants watched and calls poll()
// from its loop; every level change is one line:
//
//     130000 gpio.inject pin=27 val=1
//
// which devices/tools/trace2regtable.py reads as the part moving a line,
// with the time since the last command in its TODO list. Polling sees a
// change no sooner than the next poll() — good enough for a line that
// stays up until the application reacts (data ready, busy), not for a
// pulse shorter than the loop. For those, wire the pin to a logic
// analyzer channel next to the bus.
//
//     CaptureLines lines;              // lines go to Serial
//     void setup() { pinMode(27, INPUT); lines.watch(27); }
//     void loop()  { lines.poll(); ... }
#pragma once

#include <Arduino.h>

class CaptureLines {
 public:
  static const size_t kMax = 8;

  explicit CaptureLines(Print& out = Serial, const char* prefix = "")
      : out_(&out), prefix_(prefix) {}

  void setOutput(Print& out) { out_ = &out; }

  // Starts watching `pin` from its current level (which is not recorded:
  // the resting level belongs to the wiring, not to the part).
  bool watch(uint8_t pin) {
    if (count_ >= kMax) return false;
    pins_[count_] = pin;
    levels_[count_] = digitalRead(pin) ? 1 : 0;
    ++count_;
    return true;
  }

  void poll() {
    for (size_t i = 0; i < count_; ++i) {
      const uint8_t level = digitalRead(pins_[i]) ? 1 : 0;
      if (level == levels_[i]) continue;
      const unsigned long at = micros();
      levels_[i] = level;
      if (out_ == nullptr) continue;
      char line[48];
      snprintf(line, sizeof(line), "%lu gpio.inject pin=%u val=%u", at,
               pins_[i], level);
      out_->print(prefix_);
      out_->println(line);
    }
  }

 private:
  Print* out_;
  const char* prefix_;
  uint8_t pins_[kMax];
  uint8_t levels_[kMax];
  size_t count_ = 0;
};
