// Native environment example #2, implementation. Pure C++11.
#include "nenv.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace nenv {
namespace {

void hexOf(const uint8_t* data, size_t len, char* out, size_t cap) {
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 3 <= cap; ++i) {
    pos += snprintf(out + pos, cap - pos, "%02X", data[i]);
  }
  if (pos == 0 && cap > 0) out[0] = '\0';
}

void payloadLabel(const uint8_t* data, size_t bytes, char* out, size_t cap) {
  if (bytes <= 4) {
    char hex[12];
    hexOf(data, bytes, hex, sizeof(hex));
    snprintf(out, cap, "data=%s", hex);
  } else {
    uint8_t sum = 0;
    for (size_t i = 0; i < bytes; ++i) sum = static_cast<uint8_t>(sum + data[i]);
    snprintf(out, cap, "len=%u sum=%02X", static_cast<unsigned>(bytes), sum);
  }
}

}  // namespace

void Env::reset() {
  count_ = 0;
  nextSeq_ = 1;
  dropped_ = 0;
  nowUs_ = 0;
  nextTickUs_ = kTickUs;
  inTick_ = false;
  rxHead_ = 0;
  rxCount_ = 0;
}

// --- Bindings ----------------------------------------------------------------

bool Env::bindI2c(uint8_t address, ebdev::Device* device) {
  if (findI2c(address) != nullptr) return false;
  for (size_t i = 0; i < 2; ++i) {
    if (!i2c_[i].used) {
      i2c_[i].used = true;
      i2c_[i].address = address;
      i2c_[i].device = device;
      return true;
    }
  }
  return false;
}

void Env::bindSerial(ebdev::Device* device) { serialDevice_ = device; }

void Env::bindChannel(ebdev::Device* device) { channelDevice_ = device; }

void Env::addTicking(ebdev::Device* device) {
  if (tickingCount_ < 4) ticking_[tickingCount_++] = device;
}

ebdev::Device* Env::findI2c(uint8_t address) const {
  for (size_t i = 0; i < 2; ++i) {
    if (i2c_[i].used && i2c_[i].address == address) return i2c_[i].device;
  }
  return nullptr;
}

// --- Recording ---------------------------------------------------------------

uint32_t Env::record(const char* origin, uint32_t link, const char* fmt, ...) {
  const uint32_t seq = nextSeq_++;
  if (count_ >= kCapacity) {
    ++dropped_;
    return seq;
  }
  Event& e = events_[count_++];
  e.seq = seq;
  e.timeUs = nowUs_;
  e.tick = inTick_;
  e.origin = origin;
  e.link = link;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.text, sizeof(e.text), fmt, ap);
  va_end(ap);
  return seq;
}

size_t Env::formatTrace(char* out, size_t cap) const {
  size_t pos = 0;
  for (size_t i = 0; i < count_ && pos < cap; ++i) {
    const Event& e = events_[i];
    pos += snprintf(out + pos, cap - pos, "%02u %06llu %s %s %s", e.seq,
                    static_cast<unsigned long long>(e.timeUs),
                    e.tick ? "tick" : "main", e.origin, e.text);
    if (e.link != 0 && pos < cap) {
      pos += snprintf(out + pos, cap - pos, " re=%u", e.link);
    }
    if (pos < cap) pos += snprintf(out + pos, cap - pos, "\n");
  }
  return pos;
}

// --- Clock -------------------------------------------------------------------

void Env::advance(uint32_t us) {
  const uint64_t target = nowUs_ + us;
  while (nextTickUs_ <= target) {
    nowUs_ = nextTickUs_;
    nextTickUs_ += kTickUs;
    inTick_ = true;
    for (size_t i = 0; i < tickingCount_; ++i) ticking_[i]->advanceTo(nowUs_);
    inTick_ = false;
  }
  if (target > nowUs_) nowUs_ = target;
}

void Env::delayMicros(uint32_t us) { advance(us); }

// --- Application-side API -----------------------------------------------------

uint8_t Env::i2cWrite(uint8_t address, const uint8_t* data, size_t len) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  const uint32_t req = record("app", 0, "i2c.req addr=%02X data=%s", address, hex);
  ebdev::Device* device = findI2c(address);
  uint8_t status = 2;
  if (device != nullptr) {
    status = device->i2cWrite(data, len);
  } else {
    record("diag", req, "diag.unbound addr=%02X", address);
  }
  record("dev", req, "i2c.resp status=%u", status);
  return status;
}

size_t Env::i2cRead(uint8_t address, uint8_t* out, size_t len) {
  const uint32_t req = record("app", 0, "i2c.rd.req addr=%02X req=%u", address,
                              static_cast<unsigned>(len));
  ebdev::Device* device = findI2c(address);
  size_t count = 0;
  if (device != nullptr) {
    count = device->i2cRead(out, len);
  } else {
    record("diag", req, "diag.unbound addr=%02X", address);
  }
  char hex[12];
  hexOf(out, count, hex, sizeof(hex));
  record("dev", req, "i2c.rd.resp len=%u data=%s", static_cast<unsigned>(count),
         hex);
  return count;
}

void Env::serialWrite(const uint8_t* data, size_t len) {
  record("app", 0, "uart.tx %.*s", static_cast<int>(len),
         reinterpret_cast<const char*>(data));
  if (serialDevice_ != nullptr) serialDevice_->serialIn(data, len);
}

size_t Env::serialRead(uint8_t* out, size_t len, uint32_t timeoutUs) {
  // Mirrors Stream::readBytes: consume what is queued, otherwise wait one
  // tick slice at a time until data arrives or the timeout elapses.
  size_t got = 0;
  uint32_t waited = 0;
  while (got < len) {
    if (rxCount_ > 0) {
      const uint8_t value = rx_[rxHead_];
      rxHead_ = (rxHead_ + 1) % sizeof(rx_);
      --rxCount_;
      record("app", 0, "uart.rx %c", value);
      out[got++] = value;
      continue;
    }
    if (waited >= timeoutUs) break;
    advance(kTickUs);
    waited += kTickUs;
  }
  return got;
}

// --- Director ----------------------------------------------------------------

void Env::chanWrite(uint8_t channel, const uint8_t* data, size_t len) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  record("dir", 0, "chan.write chan=%u data=%s", channel, hex);
  if (channelDevice_ != nullptr) channelDevice_->channelWrite(channel, data, len);
}

void Env::dump(ebdev::Device* device) {
  char text[40];
  device->dump(text, sizeof(text));
  record("dir", 0, "dump %s", text);
}

// --- HostPort ----------------------------------------------------------------

uint64_t Env::nowMicros() { return nowUs_; }

void Env::lineOut(uint8_t line, uint8_t level) {
  // No pins here: the native environment records the logical line itself.
  record("dev", 0, "gpio.inject line=%u val=%u", line, level);
}

void Env::serialOut(const uint8_t* data, size_t len) {
  record("dev", 0, "dev.tx %.*s", static_cast<int>(len),
         reinterpret_cast<const char*>(data));
  for (size_t i = 0; i < len && rxCount_ < sizeof(rx_); ++i) {
    rx_[(rxHead_ + rxCount_) % sizeof(rx_)] = data[i];
    ++rxCount_;
  }
}

const char* Env::formatLabel(uint16_t id, char* out, size_t cap) const {
  if (id >= 1 && id <= 8 && formats_[id - 1].used) {
    snprintf(out, cap, "%s", formats_[id - 1].name);
  } else {
    snprintf(out, cap, "%u", id);
  }
  return out;
}

void Env::frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                   size_t bits) {
  if (bits > kMaxFrameBits) {
    record("diag", 0, "diag.frame_oversize bus=%u bits=%u max=%u", bus,
           static_cast<unsigned>(bits), kMaxFrameBits);
    return;
  }
  char payload[20];
  char label[20];
  payloadLabel(data, (bits + 7) / 8, payload, sizeof(payload));
  formatLabel(format, label, sizeof(label));
  record("dev", 0, "dev.frame bus=%u fmt=%s bits=%u %s", bus, label,
         static_cast<unsigned>(bits), payload);
}

uint16_t Env::formatId(const char* name) {
  if (name == nullptr || name[0] == '\0') return 0;
  for (size_t i = 0; i < 8; ++i) {
    if (formats_[i].used && strcmp(formats_[i].name, name) == 0) {
      return static_cast<uint16_t>(i + 1);
    }
  }
  for (size_t i = 0; i < 8; ++i) {
    if (!formats_[i].used) {
      formats_[i].used = true;
      snprintf(formats_[i].name, sizeof(formats_[i].name), "%s", name);
      return static_cast<uint16_t>(i + 1);
    }
  }
  return 0;
}

uint32_t Env::maxFrameBits(uint8_t) { return kMaxFrameBits; }

}  // namespace nenv
