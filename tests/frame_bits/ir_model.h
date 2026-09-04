// IR receiver model on the frame path: 12-bit command frames packed
// MSB-first ("acme.ir.1"), an empty frame as a trigger, and one atomic
// 128-bit status frame that is never split. Pure C++11.
#pragma once

#include <embedbench_device.h>

class IrReceiverModel : public ebdev::Device {
 public:
  static const uint8_t kBus = 0;
  static const uint8_t kChannelStatus = 0;  // world asks for the status frame

  void reset() override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void resolve();
  void status();

  uint16_t commandFormat_ = 0;
  uint16_t statusFormat_ = 0;
  bool resolved_ = false;
  uint16_t lastValue_ = 0;
  uint32_t frames_ = 0;
  uint32_t triggers_ = 0;
  uint32_t unsent_ = 0;
};
