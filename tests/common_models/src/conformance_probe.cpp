// Conformance probe implementation. Pure C++11.
#include "conformance_probe.h"

#include <stdio.h>

void ConformanceProbe::reset() {
  inCall_ = false;
  timeSeen_ = false;
  lastAdvance_ = 0;
  copyLen_ = 0;
  checks_ = 0;
  violations_ = 0;
  calls_ = 0;
}

// Every inbound method brackets itself here: a second entry while one is
// open would be the environment re-entering this device.
void ConformanceProbe::enter(const uint8_t* borrowed, size_t len) {
  if (inCall_) {
    ++violations_;  // re-entered: the contract's central promise broke
  } else {
    checks_ |= kCheckNoReentry;
  }
  inCall_ = true;
  ++calls_;

  // Borrowed buffers are valid for the duration of the call: copy now and
  // compare before returning.
  copyLen_ = 0;
  if (borrowed != nullptr) {
    for (size_t i = 0; i < len && i < sizeof(copy_); ++i) {
      copy_[copyLen_++] = borrowed[i];
    }
    bool same = true;
    for (size_t i = 0; i < copyLen_; ++i) {
      if (copy_[i] != borrowed[i]) same = false;
    }
    if (same) {
      checks_ |= kCheckBorrowedBuffer;
    } else {
      ++violations_;
    }
  }

  // Inside any device method, the environment's clock must not be behind
  // the last advanceTo it reported.
  if (timeSeen_ && port() != nullptr) {
    if (port()->nowMicros() >= lastAdvance_) {
      checks_ |= kCheckNowAgrees;
    } else {
      ++violations_;
    }
  }
}

void ConformanceProbe::leave() { inCall_ = false; }

uint8_t ConformanceProbe::i2cWrite(const uint8_t* data, size_t len,
                                   const ebdev::I2cTransfer&) {
  enter(data, len);
  leave();
  return ebdev::kI2cAck;
}

size_t ConformanceProbe::i2cRead(uint8_t* data, size_t len,
                                 const ebdev::I2cTransfer&) {
  enter(nullptr, 0);
  size_t count = 0;
  while (count < len && count < 2) {
    data[count] = static_cast<uint8_t>(count + 1);
    ++count;
  }
  leave();
  return count;
}

void ConformanceProbe::serialIn(const uint8_t* data, size_t len) {
  enter(data, len);
  leave();
}

void ConformanceProbe::lineIn(uint8_t, uint8_t) {
  enter(nullptr, 0);
  leave();
}

void ConformanceProbe::frameIn(uint8_t, uint16_t, const uint8_t* data,
                               size_t bits) {
  enter(data, ebdev::frameBytes(bits));
  leave();
}

bool ConformanceProbe::channelWrite(uint8_t channel, const uint8_t* data,
                                    size_t len) {
  enter(data, len);
  const bool known = channel == kChannelProbePort;
  if (known) probePort();
  leave();
  return known;
}

void ConformanceProbe::advanceTo(uint64_t nowUs) {
  enter(nullptr, 0);
  if (!timeSeen_) {
    timeSeen_ = true;
  } else if (nowUs > lastAdvance_) {
    checks_ |= kCheckTimeMonotonic;
  } else if (nowUs == lastAdvance_) {
    checks_ |= kCheckTimeRepeat;  // repeats are allowed, and must be safe
  } else {
    ++violations_;  // time went backwards
  }
  lastAdvance_ = nowUs;
  leave();
}

// The port-side checks: what the environment answers for frames and
// format names. Run from a channel write so the environment picks when.
void ConformanceProbe::probePort() {
  if (port() == nullptr) return;

  const uint32_t schema = ebdev::schemaFingerprint("u8 probe");
  const uint16_t first = port()->formatId("acme.probe.1", schema);
  const uint16_t again = port()->formatId("acme.probe.1", schema);
  if (first != 0 && first == again) {
    checks_ |= kCheckFormatStable;
  } else {
    ++violations_;
  }

  // A name one character over the limit must be refused, never clipped.
  char tooLong[ebdev::kFormatNameMaxLength + 2];
  for (size_t i = 0; i < sizeof(tooLong) - 1; ++i) tooLong[i] = 'a';
  tooLong[sizeof(tooLong) - 1] = '\0';
  if (port()->formatId(tooLong, schema) == 0) {
    checks_ |= kCheckFormatNameLimit;
  } else {
    ++violations_;
  }

  const uint32_t limit = port()->maxFrameBits(0);
  if (limit == 0 || first == 0) return;  // no frame routing here

  // Within the limit: accepted whole.
  uint8_t payload[8] = {0};
  const size_t withinBits = limit < 8 ? limit : 8;
  if (port()->frameOut(0, first, payload, withinBits)) {
    checks_ |= kCheckFrameAccepted;
  } else {
    ++violations_;
  }

  // Beyond the limit: refused, not truncated. The buffer is deliberately
  // large enough for the claim so a compliant environment reads nothing
  // it should not.
  uint8_t big[64] = {0};
  const size_t overBits = limit + 8 <= 8 * sizeof(big) ? limit + 8
                                                       : 8 * sizeof(big);
  if (!port()->frameOut(0, first, big, overBits)) {
    checks_ |= kCheckFrameOversizeRefused;
  } else {
    ++violations_;
  }
}

size_t ConformanceProbe::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "probe checks=%03X viol=%u calls=%u",
                         checks_, violations_, calls_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
