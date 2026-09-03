// SPI display-style composite model: byte transfers qualified by a
// data/command input line, a busy output line, and time-driven busy
// release. Pure C++11, no platform includes. Per the project stance,
// exchanges are logical byte streams — no waveforms, no bit timing.
#pragma once

#include <embedbench_device.h>

class SpiDisplayModel : public ebdev::Device {
 public:
  // Input line 0: data/command select (LOW = command byte).
  static const uint8_t kLineDc = 0;
  // Output line 0: busy indicator during a refresh.
  static const uint8_t kLineBusy = 0;
  // Command 0xFF: refresh; busy for this long.
  static const uint32_t kRefreshUs = 1000;

  void reset() override;

  void lineIn(uint8_t line, uint8_t level) override;
  uint8_t spiTransfer(uint8_t mosi) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint8_t dc_ = 0;
  uint8_t command_ = 0;
  uint32_t dataCount_ = 0;
  uint8_t checksum_ = 0;
  uint64_t busyUntilUs_ = 0;
};
