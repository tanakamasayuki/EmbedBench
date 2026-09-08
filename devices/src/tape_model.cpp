// Tape playback implementation. Pure C++11.
#include "tape_model.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

void hexOf(const uint8_t* data, size_t len, char* out, size_t cap) {
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 3 <= cap; ++i) {
    pos += snprintf(out + pos, cap - pos, "%02X", data[i]);
  }
  if (pos == 0 && cap > 0) out[0] = '\0';
}

}  // namespace

const char* TapeModel::kindName(uint8_t kind) {
  switch (kind) {
    case kWrite: return "write";
    case kRead: return "read";
    case kSerialIn: return "serial in";
    case kSerialOut: return "serial out";
    case kSpi: return "spi";
    default: return "?";
  }
}

void TapeModel::reset() {
  step_ = 0;
  armed_ = false;
  dueAtUs_ = 0;
  progress_ = 0;
  stepBad_ = false;
  exhaustedSaid_ = false;
  mismatches_ = 0;
}

const TapeStep* TapeModel::current() const {
  return step_ < spec_->count ? &spec_->steps[step_] : nullptr;
}

void TapeModel::mismatch(const char* fmt, ...) {
  ++mismatches_;
  char text[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  if (port() != nullptr) port()->diagnose(text);
}

void TapeModel::finishStep() {
  ++step_;
  progress_ = 0;
  stepBad_ = false;
  armed_ = false;
  arm();
}

void TapeModel::arm() {
  const TapeStep* s = current();
  if (s == nullptr || s->kind != kSerialOut || armed_ || port() == nullptr) return;
  armed_ = true;
  dueAtUs_ = port()->nowMicros() + s->delayUs;
  port()->requestWake(dueAtUs_);
}

uint8_t TapeModel::i2cWrite(const uint8_t* data, size_t len,
                            const ebdev::I2cTransfer& xfer) {
  const TapeStep* s = current();
  char got[24];
  hexOf(data, len, got, sizeof(got));
  if (s == nullptr) {
    if (!exhaustedSaid_) mismatch("tape ended: write %s", got);
    exhaustedSaid_ = true;
    return ebdev::kI2cAddressNack;
  }
  if (s->kind != kWrite) {
    mismatch("tape %u: want %s, got write %s", static_cast<unsigned>(step_),
             kindName(s->kind), got);
    return ebdev::kI2cDataNack;
  }
  bool same = len == s->length;
  for (size_t i = 0; same && i < len; ++i) same = data[i] == s->data[i];
  if (!same) {
    char want[24];
    hexOf(s->data, s->length, want, sizeof(want));
    mismatch("tape %u: want write %s, got %s", static_cast<unsigned>(step_),
             want, got);
  }
  if ((xfer.stop ? 1 : 0) != s->stop) {
    mismatch("tape %u: stop=%u, recorded %u", static_cast<unsigned>(step_),
             xfer.stop ? 1 : 0, s->stop);
  }
  const uint8_t status = s->status;
  finishStep();
  return status;
}

size_t TapeModel::i2cRead(uint8_t* data, size_t len,
                          const ebdev::I2cTransfer& xfer) {
  const TapeStep* s = current();
  if (s == nullptr) {
    if (!exhaustedSaid_) {
      mismatch("tape ended: read of %u", static_cast<unsigned>(len));
    }
    exhaustedSaid_ = true;
    return 0;
  }
  if (s->kind != kRead) {
    mismatch("tape %u: want %s, got read of %u", static_cast<unsigned>(step_),
             kindName(s->kind), static_cast<unsigned>(len));
    return 0;
  }
  if (s->request != 0 && len != s->request) {
    mismatch("tape %u: read of %u, recorded %u", static_cast<unsigned>(step_),
             static_cast<unsigned>(len), s->request);
  }
  if ((xfer.stop ? 1 : 0) != s->stop) {
    mismatch("tape %u: stop=%u, recorded %u", static_cast<unsigned>(step_),
             xfer.stop ? 1 : 0, s->stop);
  }
  const size_t count = len < s->length ? len : s->length;
  for (size_t i = 0; i < count; ++i) data[i] = s->data[i];
  finishStep();
  return count;
}

void TapeModel::serialIn(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const TapeStep* s = current();
    if (s == nullptr) {
      if (!exhaustedSaid_) mismatch("tape ended: serial byte %02X", data[i]);
      exhaustedSaid_ = true;
      return;
    }
    if (s->kind != kSerialIn) {
      if (!stepBad_) {
        mismatch("tape %u: want %s, got serial byte %02X",
                 static_cast<unsigned>(step_), kindName(s->kind), data[i]);
      }
      stepBad_ = true;  // said once per step; the bytes are dropped
      continue;
    }
    if (data[i] != s->data[progress_] && !stepBad_) {
      mismatch("tape %u: serial byte %u want %02X, got %02X",
               static_cast<unsigned>(step_), static_cast<unsigned>(progress_),
               s->data[progress_], data[i]);
      stepBad_ = true;
    }
    if (++progress_ >= s->length) finishStep();
  }
}

uint8_t TapeModel::spiTransfer(uint8_t mosi) {
  const TapeStep* s = current();
  if (s == nullptr) {
    if (!exhaustedSaid_) mismatch("tape ended: spi byte %02X", mosi);
    exhaustedSaid_ = true;
    return 0xFF;
  }
  if (s->kind != kSpi) {
    if (!stepBad_) {
      mismatch("tape %u: want %s, got spi byte %02X",
               static_cast<unsigned>(step_), kindName(s->kind), mosi);
    }
    stepBad_ = true;
    return 0xFF;
  }
  if (mosi != s->data[progress_] && !stepBad_) {
    mismatch("tape %u: spi byte %u want %02X, got %02X",
             static_cast<unsigned>(step_), static_cast<unsigned>(progress_),
             s->data[progress_], mosi);
    stepBad_ = true;
  }
  const uint8_t miso = s->data[s->length + progress_];
  if (++progress_ >= s->length) finishStep();
  return miso;
}

void TapeModel::advanceTo(uint64_t nowUs) {
  arm();
  for (;;) {
    const TapeStep* s = current();
    if (s == nullptr || s->kind != kSerialOut || !armed_ || nowUs < dueAtUs_) {
      return;
    }
    if (port() != nullptr) port()->serialOut(s->data, s->length);
    finishStep();  // arms the next device-initiated step from nowMicros()
  }
}

size_t TapeModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "tape step=%u/%u mismatch=%u",
                         static_cast<unsigned>(step_),
                         static_cast<unsigned>(spec_->count), mismatches_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
