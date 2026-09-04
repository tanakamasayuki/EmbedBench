// Environment implementation example #2: a minimal native recorder that
// hosts device models written against embedbench_device.h with no
// Arduino and no host core. It plays every role the host-side draft core
// plays — HostPort for devices, an application-side bus API, a director
// injection path, a virtual clock with ticks, and an ordered event trace
// in the same line format — so the same model sources can be compared
// across two independent environments. Pure C++11.
//
// Re-entrancy contract: this environment makes no callback into the
// application while a device method runs (serialOut only queues bytes,
// frames are recorded, there are no interrupts), so a device is never
// re-entered by construction. Serial bytes are binary-safe in transport
// and in the log (text when printable, payload label otherwise).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <embedbench_device.h>

namespace nenv {

class Env : public ebdev::HostPort {
 public:
  static const size_t kCapacity = 64;
  static const uint32_t kTickUs = 1000;
  static const uint32_t kMaxFrameBits = 64;

  // Clears the trace, clock, and serial queue; bindings and interned
  // format names persist like the host draft's do.
  void reset();

  // --- Bindings ------------------------------------------------------------
  bool bindI2c(uint8_t address, ebdev::Device* device);
  void bindSerial(ebdev::Device* device);
  void bindChannel(ebdev::Device* device);
  void addTicking(ebdev::Device* device);

  // --- Application-side API (what Wire / Serial1 / delay are on Arduino) --
  uint8_t i2cWrite(uint8_t address, const uint8_t* data, size_t len,
                   bool stop = true);
  size_t i2cRead(uint8_t address, uint8_t* out, size_t len,
                 bool stop = true);
  void serialWrite(const uint8_t* data, size_t len);
  size_t serialRead(uint8_t* out, size_t len, uint32_t timeoutUs);
  void delayMicros(uint32_t us);

  // Shrink the receive queue, the way a sketch can on the host core, so a
  // test can drive serialOut into its capacity-shortfall branch.
  void setRxCapacity(size_t bytes);

  // --- Director --------------------------------------------------------------
  void chanWrite(uint8_t channel, const uint8_t* data, size_t len);
  void dump(ebdev::Device* device);

  // --- HostPort ----------------------------------------------------------------
  uint64_t nowMicros() override;
  void lineOut(uint8_t line, uint8_t level) override;
  bool serialOut(const uint8_t* data, size_t len) override;
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override;
  uint16_t formatId(const char* name, uint32_t schema) override;
  uint32_t maxFrameBits(uint8_t bus) override;

  // --- Trace -------------------------------------------------------------------
  size_t formatTrace(char* out, size_t cap) const;
  size_t eventCount() const { return count_; }
  uint32_t dropped() const { return dropped_; }

 private:
  struct Event {
    uint32_t seq;
    uint64_t timeUs;
    bool tick;
    const char* origin;
    uint32_t link;
    char text[56];
  };
  struct I2cSlot {
    bool used;
    uint8_t address;
    ebdev::Device* device;
  };
  struct FormatSlot {
    bool used;
    char name[20];
    uint32_t schema;
  };

  uint32_t record(const char* origin, uint32_t link, const char* fmt, ...);
  void advance(uint32_t us);
  I2cSlot* findI2c(uint8_t address);
  const char* formatLabel(uint16_t id, char* out, size_t cap) const;

  Event events_[kCapacity];
  size_t count_ = 0;
  uint32_t nextSeq_ = 1;
  uint32_t dropped_ = 0;
  uint64_t nowUs_ = 0;
  uint64_t nextTickUs_ = kTickUs;
  bool inTick_ = false;

  I2cSlot i2c_[2] = {{false, 0, nullptr}, {false, 0, nullptr}};
  uint16_t openAddress_ = 0xFFFF;  // bus-level: last transfer without STOP
  ebdev::Device* serialDevice_ = nullptr;
  ebdev::Device* channelDevice_ = nullptr;
  ebdev::Device* ticking_[4] = {nullptr, nullptr, nullptr, nullptr};
  size_t tickingCount_ = 0;

  uint8_t rx_[64];
  size_t rxHead_ = 0;
  size_t rxCount_ = 0;
  size_t rxLimit_ = sizeof(rx_);

  FormatSlot formats_[8];
};

}  // namespace nenv
