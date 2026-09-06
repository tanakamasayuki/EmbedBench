// An IMU with a FIFO: the first model in the catalog that keeps
// sampling whether or not anyone is listening, and whose read length
// depends on how long the application took to come back.
//
// Three things here appear nowhere else. The device samples on its own
// schedule, so time passing is what produces data rather than a request.
// The buffer between the device and the application has a capacity, so
// an application that is late loses samples — and losing them silently
// is exactly the bug a verification library exists to catch, so the part
// keeps a sticky overflow flag and says so. And the burst read is
// variable length: how many bytes come back is device state, not
// something the caller already knows. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitImuModel : public ebdev::Device {
 public:
  static const uint8_t kRegStatus = 0x00;   // [count, flags]
  static const uint8_t kRegFifo = 0x10;     // burst read, 2 bytes per sample
  static const uint8_t kRegControl = 0x20;  // 1 = sampling, 0 = stopped
  static const uint8_t kFlagOverflow = 0x01;
  static const uint8_t kLineIrq = 0;
  static const uint8_t kChannelValue = 0;  // world: [hi, lo] of the next sample
  static const size_t kFifoDepth = 16;
  static const size_t kWatermark = 8;
  static const uint64_t kSampleUs = 2500;  // 400 Hz: never on a 10 ms tick

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  void advanceTo(uint64_t nowUs) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void updateIrq();

  uint8_t reg_ = 0;
  bool sampling_ = false;
  uint64_t nextSampleUs_ = 0;
  uint16_t value_ = 0;
  uint16_t fifo_[kFifoDepth] = {0};
  size_t count_ = 0;
  uint8_t flags_ = 0;
  uint8_t irq_ = 0;
  uint32_t taken_ = 0;
  uint32_t lost_ = 0;
};
