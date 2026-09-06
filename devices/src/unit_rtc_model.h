// A real-time clock: the first model whose state is a time of its own.
// Every other device in the catalog measures elapsed microseconds; this
// one keeps a wall clock in seconds, set by the application, and has to
// hold the two in step.
//
// Two consequences make it worth having. The device cannot capture its
// time base when it is attached — attach stores the port and nothing
// else — so the base is taken lazily, at the moment the application
// first sets the clock. And an alarm is a wall-clock instant that has to
// be converted back into a microsecond wake, which is the only place in
// the catalog where the device's unit and the environment's unit differ.
// Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitRtcModel : public ebdev::Device {
 public:
  static const uint8_t kRegTime = 0x00;   // 4 bytes, seconds, big endian
  static const uint8_t kRegAlarm = 0x10;  // 4 bytes, seconds, big endian
  static const uint8_t kRegStatus = 0x20;  // [fired]
  static const uint8_t kLineInt = 0;

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint32_t nowSeconds() const;
  void armAlarm();

  uint8_t reg_ = 0;
  bool based_ = false;
  uint32_t baseSec_ = 0;
  uint64_t baseUs_ = 0;
  uint32_t alarmSec_ = 0;
  bool alarmSet_ = false;
  bool fired_ = false;
  uint32_t rejected_ = 0;
};
