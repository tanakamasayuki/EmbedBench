// Remote-node model for the generic frame path: receives 16-bit command
// frames (format 1), reacts only to its own address, and answers with a
// telemetry frame (format 2) after a fixed latency. Stands in for any
// protocol without a dedicated port (IR remote, sub-GHz RF, PIO links):
// per project policy the frames are the pre-encoding logical bits plus a
// format id, never a waveform. Pure C++11.
#pragma once

#include <embedbench_device.h>

class RemoteNodeModel : public ebdev::Device {
 public:
  static const uint16_t kFormatCommand = 1;    // [address, command]
  static const uint16_t kFormatTelemetry = 2;  // [address, power]
  static const uint8_t kBus = 0;  // the logical link this node lives on
  static const uint8_t kAddress = 0x04;
  static const uint8_t kCommandPowerOn = 0x08;
  static const uint32_t kReplyLatencyUs = 1000;

  void reset() override;

  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  void advanceTo(uint64_t nowUs) override;
  size_t dump(char* out, size_t cap) override;

 private:
  uint8_t power_ = 0;
  bool hasPending_ = false;
  uint64_t replyDueUs_ = 0;
};
