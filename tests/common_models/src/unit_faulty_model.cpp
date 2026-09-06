// Misbehaving part implementation. Pure C++11.
#include "unit_faulty_model.h"

#include <stdio.h>

void UnitFaultyModel::reset() {
  fault_ = kHealthy;
  remaining_ = 0;
  limited_ = false;
  reg_ = 0;
  served_ = 0;
  failed_ = 0;
}

// One transaction's worth of misbehaviour. A fault with a count wears
// off; one without stays until the world says otherwise.
bool UnitFaultyModel::consumeFault() {
  if (fault_ == kHealthy) return false;
  if (!limited_) return true;
  if (remaining_ == 0) {
    fault_ = kHealthy;
    return false;
  }
  --remaining_;
  if (remaining_ == 0) {
    // This transaction still fails; the next one is served.
    limited_ = false;
    fault_ = kHealthy;
  }
  return true;
}

uint8_t UnitFaultyModel::i2cWrite(const uint8_t* data, size_t len,
                                  const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  if (len == 0) return ebdev::kI2cAddressNack;
  const uint8_t active = fault_;
  if (consumeFault()) {
    ++failed_;
    if (active == kAbsent) return ebdev::kI2cAddressNack;
    if (active == kRefusing) return ebdev::kI2cDataNack;
    // A short-read fault does not affect the write half.
  }
  reg_ = data[0];
  ++served_;
  return ebdev::kI2cAck;
}

size_t UnitFaultyModel::i2cRead(uint8_t* data, size_t len,
                                const ebdev::I2cTransfer& xfer) {
  (void)xfer;
  const uint8_t active = fault_;
  if (consumeFault()) {
    ++failed_;
    // Nothing comes back at all: the master sees an empty answer, not an
    // error code, because a read has no status to report one with.
    if (active == kAbsent || active == kRefusing) return 0;
    if (active == kShortRead) {
      if (len == 0) return 0;
      data[0] = 0xA0;
      return 1;  // one byte where several were asked for
    }
  }
  size_t n = kPayload;
  if (n > len) n = len;
  for (size_t i = 0; i < n; ++i) {
    data[i] = static_cast<uint8_t>(0xA0 + reg_ + i);
  }
  ++served_;
  return n;
}

bool UnitFaultyModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  if (channel != kChannelFault || len < 2) return false;
  if (data[0] > kShortRead) return false;
  fault_ = data[0];
  remaining_ = data[1];
  limited_ = data[1] != 0;
  return true;
}

size_t UnitFaultyModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "faulty mode=%u left=%u ok=%u bad=%u",
                         fault_, remaining_, served_, failed_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
