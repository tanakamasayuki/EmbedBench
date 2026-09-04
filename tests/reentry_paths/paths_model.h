// A device that raises effects from EVERY inbound path — lineIn, frameIn,
// advanceTo, i2cRead — and counts its own call depth, so the environment's
// re-entrancy contract can be measured on all of them, plus a burst mode
// that raises more interrupts than the environment can hold. Pure C++11.
#pragma once

#include <embedbench_device.h>

class PathsModel : public ebdev::Device {
 public:
  static const uint8_t kLineIrq = 0;      // output line
  static const uint8_t kLineTrigger = 0;  // input line
  static const uint8_t kChannelBurst = 0;

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  void lineIn(uint8_t line, uint8_t level) override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

  uint32_t maxDepth() const { return maxDepth_; }

 private:
  void enter();
  void leave();
  void pulseIrq();
  void resolve();

  uint16_t statusFormat_ = 0;
  uint16_t commandFormat_ = 0;
  bool resolved_ = false;
  bool statusSent_ = false;
  uint8_t burst_ = 0;
  uint32_t reads_ = 0;
  uint32_t commands_ = 0;
  uint32_t depth_ = 0;
  uint32_t maxDepth_ = 0;
};
