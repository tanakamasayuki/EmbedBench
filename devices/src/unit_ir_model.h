// An IR unit (NEC remote receiver/transmitter) on the generic frame path.
// The shape nothing else in the catalog has: the link is a broadcast with
// no addressing, and a held button produces a stream of repeats that the
// application has to collapse back into one press.
//
// Per project policy the frame carries the pre-encoding logical bits, so
// the payload here is [address, command] — NEC's inverted duplicates and
// its pulse-distance encoding are physical layer and never appear. The
// repeat is NEC's repeat code, which carries no payload at all, so it is
// sent as an empty frame (bits == 0), which the interface allows.
// Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitIrModel : public ebdev::Device {
 public:
  static const uint8_t kBus = 0;
  // world: [address, command, repeats]
  static const uint8_t kChannelPress = 0;
  static const uint8_t kChannelLastRx = 1;  // read back: [address, command]
  static const uint64_t kRepeatPeriodUs = 2000;

  void reset() override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  void advanceTo(uint64_t nowUs) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void resolve();
  void emit(bool repeat);

  uint16_t codeFormat_ = 0;
  uint16_t repeatFormat_ = 0;
  bool resolved_ = false;
  uint8_t address_ = 0;
  uint8_t command_ = 0;
  uint8_t repeatsLeft_ = 0;
  uint64_t nextDueUs_ = 0;
  bool holding_ = false;
  uint8_t rxAddress_ = 0;
  uint8_t rxCommand_ = 0;
  uint32_t sent_ = 0;
  uint32_t received_ = 0;
};
