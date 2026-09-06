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

// CRC-8/ATM, matching the host environment example: a byte sum cannot see
// a reordering, which bulk payloads are full of (tests/bulk_checksum).
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

void payloadLabel(const uint8_t* data, size_t bytes, char* out, size_t cap) {
  if (bytes == 0) {
    snprintf(out, cap, "empty");
    return;
  }
  if (bytes <= 4) {
    char hex[12];
    hexOf(data, bytes, hex, sizeof(hex));
    snprintf(out, cap, "data=%s", hex);
  } else {
    snprintf(out, cap, "len=%u crc=%02X", static_cast<unsigned>(bytes),
             crc8(data, bytes));
  }
}

// Text when every byte is printable ASCII, binary payload label otherwise.
// Serial bytes print as text when every byte is printable ASCII or one
// of the line-ending characters real protocols are full of, which are
// escaped so the line stays one line. Anything else (a NUL, a high byte)
// falls back to the binary payload label, so no byte value can cut a
// record short.
void bytesLabel(const uint8_t* data, size_t len, char* out, size_t cap) {
  bool renderable = len > 0;
  size_t needed = 0;
  for (size_t i = 0; i < len && renderable; ++i) {
    const uint8_t b = data[i];
    if (b >= 0x20 && b <= 0x7E) {
      needed += 1;
    } else if (b == '\r' || b == '\n' || b == '\t') {
      needed += 2;  // written as \r, \n, \t
    } else {
      renderable = false;
    }
  }
  if (!renderable || needed + 1 > cap) {
    payloadLabel(data, len, out, cap);
    return;
  }
  size_t pos = 0;
  for (size_t i = 0; i < len; ++i) {
    const uint8_t b = data[i];
    if (b >= 0x20 && b <= 0x7E) {
      out[pos++] = static_cast<char>(b);
    } else {
      out[pos++] = '\\';
      out[pos++] = b == '\r' ? 'r' : (b == '\n' ? 'n' : 't');
    }
  }
  out[pos] = '\0';
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
  openAddress_ = 0xFFFF;
  for (size_t i = 0; i < kWakeSlots; ++i) wakeAtUs_[i] = 0;
  for (size_t i = 0; i < 4; ++i) analog_[i] = 0;
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

void Env::setRxCapacity(size_t bytes) {
  rxLimit_ = bytes < sizeof(rx_) ? bytes : sizeof(rx_);
  while (rxCount_ > rxLimit_) --rxCount_;
}

void Env::bindChannel(ebdev::Device* device) { channelDevice_ = device; }

void Env::addTicking(ebdev::Device* device) {
  if (tickingCount_ < 4) ticking_[tickingCount_++] = device;
}

Env::I2cSlot* Env::findI2c(uint8_t address) {
  for (size_t i = 0; i < 2; ++i) {
    if (i2c_[i].used && i2c_[i].address == address) return &i2c_[i];
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
  for (;;) {
    // Stop at the next tick boundary, or earlier at a device's requested
    // wake time, so a latency that does not divide by the tick is served
    // when it is due rather than at the boundary after it.
    uint64_t next = nextTickUs_;
    bool isWake = false;
    const uint64_t wake = nextWakeAfter(nowUs_);
    if (wake != 0 && wake < next) {
      next = wake;
      isWake = true;
    }
    if (next > target) break;
    nowUs_ = next;
    if (!isWake) nextTickUs_ += kTickUs;
    retireWakesUpTo(nowUs_);
    inTick_ = true;
    for (size_t i = 0; i < tickingCount_; ++i) ticking_[i]->advanceTo(nowUs_);
    inTick_ = false;
  }
  if (target > nowUs_) nowUs_ = target;
}

void Env::delayMicros(uint32_t us) { advance(us); }

// --- Application-side API -----------------------------------------------------

uint8_t Env::i2cWrite(uint8_t address, const uint8_t* data, size_t len,
                      bool stop) {
  char hex[12];
  hexOf(data, len, hex, sizeof(hex));
  I2cSlot* slot = findI2c(address);
  const bool continued = slot != nullptr && openAddress_ == address;
  const uint32_t req = record("app", 0, "i2c.req addr=%02X data=%s stop=%u%s",
                              address, hex, stop ? 1 : 0, continued ? " rs" : "");
  uint8_t status = ebdev::kI2cAddressNack;
  if (slot != nullptr) {
    const ebdev::I2cTransfer xfer = {stop, continued};
    status = slot->device->i2cWrite(data, len, xfer);
    if (status > ebdev::kI2cOther) {
      record("diag", req, "diag.i2c_status addr=%02X status=%u", address, status);
    }
  } else {
    record("diag", req, "diag.unbound addr=%02X", address);
  }
  openAddress_ = stop ? 0xFFFF : address;  // any transfer moves the bus state
  record("dev", req, "i2c.resp status=%u", status);
  return status;
}

size_t Env::i2cRead(uint8_t address, uint8_t* out, size_t len, bool stop) {
  I2cSlot* slot = findI2c(address);
  const bool continued = slot != nullptr && openAddress_ == address;
  const uint32_t req = record("app", 0, "i2c.rd.req addr=%02X req=%u stop=%u%s",
                              address, static_cast<unsigned>(len), stop ? 1 : 0,
                              continued ? " rs" : "");
  size_t count = 0;
  if (slot != nullptr) {
    const ebdev::I2cTransfer xfer = {stop, continued};
    count = slot->device->i2cRead(out, len, xfer);
    if (count > len) {
      record("diag", req, "diag.i2c_read_length addr=%02X got=%u max=%u", address,
             static_cast<unsigned>(count), static_cast<unsigned>(len));
      count = 0;
    }
  } else {
    record("diag", req, "diag.unbound addr=%02X", address);
  }
  openAddress_ = stop ? 0xFFFF : address;
  char hex[12];
  hexOf(out, count, hex, sizeof(hex));
  record("dev", req, "i2c.rd.resp len=%u data=%s", static_cast<unsigned>(count),
         hex);
  return count;
}

void Env::serialWrite(const uint8_t* data, size_t len) {
  char text[24];
  bytesLabel(data, len, text, sizeof(text));
  record("app", 0, "uart.tx %s", text);
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
      if (value >= 0x20 && value <= 0x7E) {
        record("app", 0, "uart.rx %c", value);
      } else {
        record("app", 0, "uart.rx 0x%02X", value);
      }
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
  if (channelDevice_ != nullptr &&
      !channelDevice_->channelWrite(channel, data, len)) {
    record("diag", 0, "diag.chan_reject chan=%u len=%u", channel,
           static_cast<unsigned>(len));
  }
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

bool Env::serialOut(const uint8_t* data, size_t len) {
  char text[24];
  bytesLabel(data, len, text, sizeof(text));
  record("dev", 0, "dev.tx %s", text);
  size_t accepted = 0;
  for (; accepted < len && rxCount_ < rxLimit_; ++accepted) {
    rx_[(rxHead_ + rxCount_) % sizeof(rx_)] = data[accepted];
    ++rxCount_;
  }
  if (accepted < len) {
    record("diag", 0, "diag.uart_rx_full accepted=%u len=%u",
           static_cast<unsigned>(accepted), static_cast<unsigned>(len));
    return false;
  }
  return true;
}

const char* Env::formatLabel(uint16_t id, char* out, size_t cap) const {
  if (id >= 1 && id <= 8 && formats_[id - 1].used) {
    snprintf(out, cap, "%s", formats_[id - 1].name);
  } else {
    snprintf(out, cap, "%u", id);
  }
  return out;
}

bool Env::frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                   size_t bits) {
  if (format == 0) {
    record("diag", 0, "diag.frame_noformat bus=%u", bus);
    return false;
  }
  if (format > 8 || !formats_[format - 1].used) {
    record("diag", 0, "diag.frame_unknown_format bus=%u fmt=%u", bus, format);
    return false;
  }
  if (bits > kMaxFrameBits) {
    record("diag", 0, "diag.frame_oversize bus=%u bits=%u max=%u", bus,
           static_cast<unsigned>(bits), kMaxFrameBits);
    return false;
  }
  if (bits > 0 && (data == nullptr || !ebdev::framePaddingClean(data, bits))) {
    record("diag", 0, "diag.frame_padding bus=%u bits=%u", bus,
           static_cast<unsigned>(bits));
    return false;
  }
  char payload[20];
  char label[20];
  payloadLabel(data, ebdev::frameBytes(bits), payload, sizeof(payload));
  formatLabel(format, label, sizeof(label));
  record("dev", 0, "dev.frame bus=%u fmt=%s bits=%u %s", bus, label,
         static_cast<unsigned>(bits), payload);
  return true;
}

uint16_t Env::formatId(const char* name, uint32_t schema) {
  if (name == nullptr || name[0] == '\0') return 0;
  if (strlen(name) > ebdev::kFormatNameMaxLength) {
    record("diag", 0, "diag.fmt_name_long len=%u",
           static_cast<unsigned>(strlen(name)));
    return 0;
  }
  for (size_t i = 0; i < 8; ++i) {
    if (formats_[i].used && strcmp(formats_[i].name, name) == 0) {
      if (formats_[i].schema != schema) {
        record("diag", 0, "diag.fmt_conflict name=%s", name);
        return 0;
      }
      return static_cast<uint16_t>(i + 1);
    }
  }
  for (size_t i = 0; i < 8; ++i) {
    if (!formats_[i].used) {
      formats_[i].used = true;
      formats_[i].schema = schema;
      snprintf(formats_[i].name, sizeof(formats_[i].name), "%s", name);
      return static_cast<uint16_t>(i + 1);
    }
  }
  return 0;
}

uint32_t Env::maxFrameBits(uint8_t) { return kMaxFrameBits; }

// The device presents a voltage: recorded, then held for the application
// to read, with no director step in between (interface revision 002).
bool Env::analogOut(uint8_t line, uint16_t raw) {
  if (line >= 4) return false;
  record("dev", 0, "analog.inject line=%u val=%u", line, raw);
  analog_[line] = raw;
  return true;
}

uint16_t Env::analogValue(uint8_t line) const {
  return line < 4 ? analog_[line] : 0;
}

// A wake request (revision 003): the clock stops there as well as at its
// tick boundaries, so a latency that does not divide by the tick is still
// served when it is due. The earliest outstanding request wins.
uint64_t Env::nextWakeAfter(uint64_t after) const {
  uint64_t best = 0;
  for (size_t i = 0; i < kWakeSlots; ++i) {
    const uint64_t w = wakeAtUs_[i];
    if (w == 0 || w <= after) continue;
    if (best == 0 || w < best) best = w;
  }
  return best;
}

void Env::retireWakesUpTo(uint64_t when) {
  for (size_t i = 0; i < kWakeSlots; ++i) {
    if (wakeAtUs_[i] != 0 && wakeAtUs_[i] <= when) wakeAtUs_[i] = 0;
  }
}

bool Env::requestWake(uint64_t whenUs) {
  // A time already past asks for the next possible advance, which the
  // boundary loop reaches anyway.
  if (whenUs <= nowUs_) return true;
  for (size_t i = 0; i < kWakeSlots; ++i) {
    if (wakeAtUs_[i] == whenUs) return true;
  }
  for (size_t i = 0; i < kWakeSlots; ++i) {
    if (wakeAtUs_[i] == 0) {
      wakeAtUs_[i] = whenUs;
      return true;
    }
  }
  record("diag", 0, "diag.wake_full pending=%u",
         static_cast<unsigned>(kWakeSlots));
  return false;
}

// Device commentary (revision 004), in order among the events.
bool Env::diagnose(const char* text) {
  record("dev", 0, "dev.note %s", text);
  return true;
}

}  // namespace nenv
