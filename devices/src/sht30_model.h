// SHT30 humidity and temperature sensor (Sensirion SHT3x-DIS), as on the
// M5Stack ENV III (U001-C, 0x44). A command part, so hand-written rather
// than a table (X67): the second model built against a real recording
// (REAL_DEVICE_ROADMAP L2). What the recording showed decides the
// behaviour; the datasheet fills in what it did not exercise.
//
//   - Single-shot measurement, three repeatabilities, with or without
//     clock stretching. While it runs the part refuses its address, to
//     reads (the NACK polling a library does) and to commands alike.
//   - A finished result is read once: six bytes, temperature then
//     humidity, each word followed by its CRC-8. A second read answers
//     nothing until the next command. A result nobody read is kept and
//     handed to the first read after the next command, at once, while
//     the new measurement runs (what looked like "answers immediately").
//   - Just after a soft reset the part takes its address and refuses the
//     command (a data NACK). After a soft reset and before any command,
//     every read gets 0000 with its CRC, then FF. An unread result
//     survives the reset.
//   - Status (F32D), clear status (3041), soft reset (30A2), heater.
//     Periodic mode is not modelled: its commands are refused and set the
//     status's "command not processed" bit, with a diagnostic.
//
// The frozen interface has no stretched read: a read is answered at the
// moment it is made. A stretching read made before the result is ready is
// answered with the result, as a master that waits does. A master that
// gives up first is the bus's property, not the part's; channel
// kChannelStretchLimit says how long it waits (the pilot's ESP32-S3 gave
// up on a 11 ms stretch and sat through a 3 ms one).
//
// The world sets what is measured: channel kChannelSamples takes raw
// (temperature, humidity) word pairs, one pair per measurement, the
// sequence repeating. Pure C++11.
#pragma once

#include <embedbench_device.h>

class Sht30Model : public ebdev::Device {
 public:
  enum Repeatability : uint8_t { kHigh, kMedium, kLow };

  static const uint8_t kChannelSamples = 0;       // n x 4 bytes, big endian
  static const uint8_t kChannelStretchLimit = 1;  // uint32 us, big endian
  static const size_t kMaxSamples = 16;

  // Measurement durations: inside every window the recordings allow (last
  // refused poll or command, first answered one), over the pilot and the
  // OEP runs at 100 and 400 kHz: high 12.376-12.416 ms, medium
  // 4.396-4.661 ms, low 2.392-2.736 ms. The datasheet's typical values are
  // 12.5, 4.5 and 2.5 ms; high is a little shorter on this part. Physical.
  static const uint64_t kHighUs = 12400;
  // Physical.
  static const uint64_t kMediumUs = 4500;
  // Physical.
  static const uint64_t kLowUs = 2500;
  // Soft reset to accepting commands: datasheet maximum 1.5 ms. Physical.
  static const uint64_t kSoftResetUs = 1500;

  static const uint16_t kStatusAlert = 0x8000;
  static const uint16_t kStatusHeater = 0x2000;
  static const uint16_t kStatusReset = 0x0010;
  static const uint16_t kStatusCommand = 0x0002;

  static uint8_t crc8(const uint8_t* data, size_t len);

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  enum Pending : uint8_t { kNothing, kMeasurement, kStatus, kReset };
  uint64_t now() const;
  bool busy(uint64_t t) const;
  void start(Repeatability r, bool stretch);
  size_t answer(uint8_t* data, size_t len, const uint8_t* words, size_t count);

  uint16_t status_ = kStatusAlert | kStatusReset;
  Pending pending_ = kNothing;
  bool stretch_ = false;
  uint64_t readyAtUs_ = 0;  // the part refuses its address until then
  uint8_t samples_[kMaxSamples * 4];
  size_t sampleCount_ = 0;
  size_t nextSample_ = 0;
  uint8_t result_[4] = {0, 0, 0, 0};
  uint8_t held_[4] = {0, 0, 0, 0};  // a finished result nobody read
  bool holding_ = false;
  bool afterReset_ = false;  // soft reset, no command since
  uint32_t stretchLimitUs_ = 0;  // 0: the master waits as long as it takes
  uint32_t measurements_ = 0;
  uint32_t refused_ = 0;
  uint32_t stretched_ = 0;
  uint32_t gaveUp_ = 0;
};
