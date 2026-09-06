// A LoRa unit on the generic frame path: the long-air-time radio. Two
// things make this shape different from every other model in the catalog.
//
// First, sending is not instant — the air time grows with the payload,
// the radio is deaf while transmitting, and a second send during that
// window is dropped rather than queued. Second, a received frame arrives
// with signal quality that is NOT part of the payload; RSSI and SNR are
// properties of the reception, so they leave through channelRead instead
// of being smuggled into the frame bits.
//
// Per project policy the frames are the logical payload: spreading
// factor, preamble, coding rate and the chirp itself are physical layer
// and never appear. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitLoraModel : public ebdev::Device {
 public:
  static const uint8_t kBus = 1;  // a different link from the IR unit
  static const uint8_t kLineTxDone = 0;
  // world: [rssi as a negative magnitude, snr, payload...]
  static const uint8_t kChannelDownlink = 0;
  static const uint8_t kChannelStatus = 1;  // read: [rssi, snr, busy]
  static const uint64_t kAirTimePerByteUs = 1000;
  static const size_t kMaxPayloadBytes = 4;  // the radio's own limit

  void reset() override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  void advanceTo(uint64_t nowUs) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void resolve();

  uint16_t uplinkFormat_ = 0;
  uint16_t downlinkFormat_ = 0;
  bool resolved_ = false;
  bool busy_ = false;
  uint64_t txDoneUs_ = 0;
  uint32_t sent_ = 0;
  uint32_t dropped_ = 0;
  uint32_t received_ = 0;
  uint8_t rssi_ = 0;
  uint8_t snr_ = 0;
};
