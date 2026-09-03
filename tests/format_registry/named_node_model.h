// Remote-node model that identifies its frame formats by NAME and lets
// the environment intern them into local ids (HostPort::formatId). The
// model never hard-codes a number, so independent libraries cannot
// collide. Pure C++11.
#pragma once

#include <embedbench_device.h>

class NamedNodeModel : public ebdev::Device {
 public:
  static const uint8_t kBus = 0;
  static const uint8_t kAddress = 0x04;
  static const uint8_t kCommandPowerOn = 0x08;
  static const uint32_t kReplyLatencyUs = 1000;

  void reset() override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void resolveFormats();

  uint16_t commandId_ = 0;
  uint16_t telemetryId_ = 0;
  bool resolved_ = false;
  uint8_t power_ = 0;
  bool hasPending_ = false;
  uint64_t replyDueUs_ = 0;
};
