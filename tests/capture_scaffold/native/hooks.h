// The hand-written half of the scaffold workflow: each class below is
// what a developer writes after reading the TODO list in the generated
// header — the behavior the capture could not say, as hooks on the table
// it could. The generated headers stay untouched; regenerating them from
// a new capture does not lose any of this.
#pragma once

#include "env_table.h"
#include "imu_table.h"
#include "temp_table.h"

// [temp begin]
// TODO from the scaffold: line 0 rose right after chan.write chan=0.
class TempHooked : public TempTable {
 public:
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override {
    if (!TempTable::channelWrite(channel, data, len)) return false;
    if (port() != nullptr) port()->lineOut(0, 1);
    return true;
  }
};
// [temp end]

// [env begin]
// TODO from the scaffold: F3 changed 08 -> 00 with only time passing;
// line 0 rose 7500 us after write F4=25; chan 0 payload is transformed
// before it appears in FA; F3's power-on contents were uncertain.
class EnvHooked : public EnvTable {
 public:
  // Physical: a BME280 forced measurement, as in env_sensor_model.
  static const uint64_t kMeasureUs = 7500;

  void reset() override {
    EnvTable::reset();
    set(0xF3, 0, 0x00);  // the capture first saw F3 mid-measurement
    measuring_ = false;
    readyAtUs_ = 0;
    raw_ = 0;
  }
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override {
    if (channel != 0 || len != 3) return false;
    raw_ = (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8) | data[2];
    return true;
  }
  void advanceTo(uint64_t nowUs) override {
    if (!measuring_ || nowUs < readyAtUs_) return;
    measuring_ = false;
    const uint8_t temp[3] = {static_cast<uint8_t>((raw_ >> 12) & 0xFF),
                             static_cast<uint8_t>((raw_ >> 4) & 0xFF),
                             static_cast<uint8_t>((raw_ << 4) & 0xF0)};
    setBytes(0xFA, temp, 3);
    set(0xF3, 0, 0x00);
    if (port() != nullptr) port()->lineOut(0, 1);
  }

 protected:
  void afterWrite(uint8_t address, const uint8_t* value, size_t len) override {
    if (address != 0xF4 || len < 1 || value[0] != 0x25 || measuring_) return;
    if (port() == nullptr) return;
    measuring_ = true;
    readyAtUs_ = port()->nowMicros() + kMeasureUs;
    set(0xF3, 0, 0x08);
    port()->requestWake(readyAtUs_);
  }

 private:
  bool measuring_ = false;
  uint64_t readyAtUs_ = 0;
  uint32_t raw_ = 0;
};
// [env end]

// [imu begin]
// TODO from the scaffold: register 00 changed with time and after a read
// of 10; register 10 answered 16 bytes to a 40-byte read and the log kept
// 5 of them; line 0 moved on the device's own schedule and during the
// read of 10; chan 0 payload appears inside 10, not verbatim. In other
// words: the FIFO. The table contributes the control register only.
class ImuHooked : public ImuTable {
 public:
  // Physical: 400 Hz, as in unit_imu_model.
  static const uint64_t kSampleUs = 2500;
  static const size_t kDepth = 16;
  static const size_t kWatermark = 8;

  void reset() override {
    ImuTable::reset();
    sampling_ = false;
    nextSampleUs_ = 0;
    value_ = 0;
    count_ = 0;
    flags_ = 0;
    irq_ = 0;
  }
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override {
    if (channel != 0 || len < 2) return false;
    value_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
    return true;
  }
  void advanceTo(uint64_t nowUs) override {
    if (!sampling_) return;
    while (nowUs >= nextSampleUs_) {
      if (count_ < kDepth) {
        fifo_[count_++] = value_;
      } else if ((flags_ & 0x01) == 0) {
        flags_ |= 0x01;
        if (port() != nullptr) port()->diagnose("fifo overflow: samples lost");
      }
      nextSampleUs_ += kSampleUs;
    }
    updateIrq();
    if (port() != nullptr) port()->requestWake(nextSampleUs_);
  }

 protected:
  void afterWrite(uint8_t address, const uint8_t* value, size_t len) override {
    if (address != 0x20 || len < 1) return;
    const bool wanted = value[0] != 0;
    if (wanted && !sampling_ && port() != nullptr) {
      nextSampleUs_ = port()->nowMicros() + kSampleUs;
      port()->requestWake(nextSampleUs_);
    }
    sampling_ = wanted;
  }
  size_t onRead(uint8_t address, uint8_t* out, size_t len) override {
    if (address == 0x00) {
      if (len < 2) return 0;
      out[0] = static_cast<uint8_t>(count_);
      out[1] = flags_;
      flags_ = 0;
      return 2;
    }
    if (address != 0x10) return kPassThrough;
    size_t samples = len / 2;
    if (samples > count_) samples = count_;
    for (size_t i = 0; i < samples; ++i) {
      out[i * 2] = static_cast<uint8_t>(fifo_[i] >> 8);
      out[i * 2 + 1] = static_cast<uint8_t>(fifo_[i] & 0xFF);
    }
    for (size_t i = samples; i < count_; ++i) fifo_[i - samples] = fifo_[i];
    count_ -= samples;
    updateIrq();
    return samples * 2;
  }

 private:
  void updateIrq() {
    const uint8_t want = count_ >= kWatermark ? 1 : 0;
    if (want == irq_) return;
    irq_ = want;
    if (port() != nullptr) port()->lineOut(0, irq_);
  }

  bool sampling_ = false;
  uint64_t nextSampleUs_ = 0;
  uint16_t value_ = 0;
  uint16_t fifo_[kDepth] = {0};
  size_t count_ = 0;
  uint8_t flags_ = 0;
  uint8_t irq_ = 0;
};
// [imu end]
