// A Modbus RTU slave behind an RS485 unit: binary framing on a serial
// port, with a CRC and an address. It is the counterpart of the GPS — the
// other kind of serial device, where the payload is not text and a wrong
// checksum means silence rather than an error reply. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitModbusModel : public ebdev::Device {
 public:
  static const uint8_t kAddress = 0x11;
  static const uint8_t kFuncReadHolding = 0x03;
  static const size_t kRegisterCount = 4;
  static const uint8_t kChannelRegister = 0;  // world: [index, hi, lo]
  // A request is complete once it has been quiet for this long, the way
  // RTU framing works on a real line.
  static const uint64_t kFrameGapUs = 1500;

  void reset() override;
  void serialIn(const uint8_t* data, size_t len) override;
  void advanceTo(uint64_t nowUs) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

  static uint16_t crc16(const uint8_t* data, size_t len);

 private:
  void handleFrame();

  uint8_t frame_[16] = {0};
  size_t frameLength_ = 0;
  uint64_t idleAtUs_ = 0;
  bool pending_ = false;
  uint16_t registers_[kRegisterCount] = {0, 0, 0, 0};
  uint32_t answered_ = 0;
  uint32_t badCrc_ = 0;
  uint32_t foreign_ = 0;
};
