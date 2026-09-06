// A part that misbehaves on purpose. Everything else in the catalog
// answers correctly; this one is here so a test can drive the failures a
// bench cannot reproduce on demand — a part that is not powered yet, a
// connection that drops for a few transactions and comes back, and a
// read that stops short of what was asked for.
//
// It deliberately does NOT diagnose its own faults. A part that has
// fallen off the bus does not announce it; the only evidence is the
// status the master sees and the byte count that came back, which is
// exactly what the application under test has to cope with.
// Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitFaultyModel : public ebdev::Device {
 public:
  enum Fault : uint8_t {
    kHealthy = 0,
    kAbsent = 1,     // no answer at all: the address is not acknowledged
    kRefusing = 2,   // answers the address, rejects the payload
    kShortRead = 3,  // returns fewer bytes than the master asked for
  };
  // world: [fault, transactions] — how it misbehaves and for how many
  // transactions before it recovers. 0 transactions means "until told
  // otherwise".
  static const uint8_t kChannelFault = 0;
  static const size_t kPayload = 4;

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t dump(char* out, size_t cap) override;

 private:
  bool consumeFault();

  uint8_t fault_ = kHealthy;
  uint32_t remaining_ = 0;  // 0 with a fault set means "indefinitely"
  bool limited_ = false;
  uint8_t reg_ = 0;
  uint32_t served_ = 0;
  uint32_t failed_ = 0;
};
