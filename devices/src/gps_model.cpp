// GPS receiver model implementation. Pure C++11.
#include "gps_model.h"

#include <stdio.h>
#include <string.h>

void GpsModel::reset() {
  lineLength_ = 0;
  running_ = false;
  periodUs_ = kDefaultPeriodUs;
  nextUs_ = 0;
  fix_ = 1;
  sentences_ = 0;
  rejected_ = 0;
}

void GpsModel::arm(uint64_t nowUs) {
  nextUs_ = nowUs + periodUs_;
  // One-shot: a periodic device re-arms after every emission, which is
  // what "the environment may advance more often than asked" allows.
  if (port() != nullptr) port()->requestWake(nextUs_);
}

void GpsModel::serialIn(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = static_cast<char>(data[i]);
    if (c == kTerminator) {
      dispatch();
    } else if (lineLength_ < sizeof(line_) - 1) {
      line_[lineLength_++] = c;
    } else {
      // A command longer than the buffer: dropped, and said out loud
      // because a serial port has no way to refuse.
      ++rejected_;
      if (port() != nullptr) port()->diagnose("command too long");
      lineLength_ = 0;
    }
  }
}

void GpsModel::dispatch() {
  line_[lineLength_] = '\0';
  if (strcmp(line_, "START") == 0) {
    running_ = true;
    if (port() != nullptr) arm(port()->nowMicros());
  } else if (strcmp(line_, "STOP") == 0) {
    running_ = false;
  } else {
    ++rejected_;
    if (port() != nullptr) port()->diagnose("unknown command");
  }
  lineLength_ = 0;
}

void GpsModel::emit(uint64_t nowUs) {
  // "$GPGGA,<time>,<fix>*<checksum>\r\n" — the shape of the real thing,
  // with the checksum the protocol defines over the body.
  char body[24];
  const int bodyLen = snprintf(body, sizeof(body), "GPGGA,%llu,%u",
                               static_cast<unsigned long long>(nowUs / 1000),
                               fix_);
  uint8_t checksum = 0;
  for (int i = 0; i < bodyLen; ++i) {
    checksum = static_cast<uint8_t>(checksum ^ static_cast<uint8_t>(body[i]));
  }
  char sentence[40];
  const int len = snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", body,
                           checksum);
  if (port() != nullptr && len > 0) {
    port()->serialOut(reinterpret_cast<const uint8_t*>(sentence),
                      static_cast<size_t>(len));
  }
  ++sentences_;
}

bool GpsModel::channelWrite(uint8_t channel, const uint8_t* data, size_t len) {
  if (channel != kChannelFix || len != 1) return false;
  fix_ = data[0];
  return true;
}

void GpsModel::advanceTo(uint64_t nowUs) {
  if (!running_) return;
  while (nowUs >= nextUs_) {
    emit(nowUs);
    arm(nowUs);
  }
}

size_t GpsModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "gps run=%u fix=%u sent=%u rejected=%u",
                         running_ ? 1 : 0, fix_, sentences_, rejected_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
