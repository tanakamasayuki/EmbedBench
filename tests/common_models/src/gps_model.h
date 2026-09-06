// A GPS receiver on a serial port: it emits a fixed sentence on its own
// schedule and answers configuration commands. The period is device
// behaviour driven by advanceTo(), and the sentence carries the checksum
// the real protocol carries, so an application-side parser has something
// real to chew on. Pure C++11.
#pragma once

#include <embedbench_device.h>

class GpsModel : public ebdev::Device {
 public:
  // Commands end with '\n', the way a line-based protocol does.
  static const char kTerminator = '\n';
  static const uint64_t kDefaultPeriodUs = 2000;
  static const uint8_t kChannelFix = 0;  // the world sets the fix quality

  void reset() override;

  void serialIn(const uint8_t* data, size_t len) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

  uint32_t sentences() const { return sentences_; }

 private:
  void dispatch();
  void emit(uint64_t nowUs);
  void arm(uint64_t nowUs);

  char line_[24] = {0};
  size_t lineLength_ = 0;
  bool running_ = false;
  uint64_t periodUs_ = kDefaultPeriodUs;
  uint64_t nextUs_ = 0;
  uint8_t fix_ = 1;
  uint32_t sentences_ = 0;
  uint32_t rejected_ = 0;
};
