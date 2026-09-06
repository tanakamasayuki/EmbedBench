// A UWB anchor on the generic frame path. Two shapes appear here that no
// other model in the catalog has.
//
// First, several identical anchors sit on one link and answer the same
// broadcast poll; each one waits for its own slot so the replies do not
// collide, so the ordering of the answers is the behaviour under test.
// Second, the poll is 12 bits — a bit-packed header that is not a whole
// number of bytes, which is what the interface's MSB-first packing and
// its clean-padding rule exist for.
//
// Per project policy the anchor reports a distance it has already worked
// out. Time of flight is picoseconds of physical layer, far below the
// microsecond clock and deliberately out of scope: what is modelled is
// what the application does with the answers. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitUwbModel : public ebdev::Device {
 public:
  static const uint8_t kBus = 2;
  static const uint8_t kChannelDistance = 0;  // world: [mm hi, mm lo]
  // Two-way ranging turnaround is a few hundred microseconds. Physical.
  static const uint64_t kBaseLatencyUs = 500;
  // A TDMA slot wide enough to separate anchors' replies. Physical in
  // magnitude; the exact width is this model's choice.
  static const uint64_t kSlotUs = 300;

  explicit UnitUwbModel(uint8_t anchorId) : anchorId_(anchorId) {}

  void reset() override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  void advanceTo(uint64_t nowUs) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void resolve();

  uint8_t anchorId_;
  uint16_t pollFormat_ = 0;
  uint16_t respFormat_ = 0;
  bool resolved_ = false;
  uint16_t distanceMm_ = 0;
  bool pending_ = false;
  uint64_t replyDueUs_ = 0;
  uint8_t tag_ = 0;
  uint8_t seq_ = 0;
  uint32_t answered_ = 0;
  uint32_t collided_ = 0;
};
