// All-paths re-entrancy probe model implementation. Pure C++11.
#include "paths_model.h"

#include <stdio.h>

void PathsModel::reset() {
  statusFormat_ = 0;
  commandFormat_ = 0;
  resolved_ = false;
  statusSent_ = false;
  burst_ = 0;
  reads_ = 0;
  commands_ = 0;
  depth_ = 0;
  maxDepth_ = 0;
}

void PathsModel::enter() {
  ++depth_;
  if (depth_ > maxDepth_) maxDepth_ = depth_;
}

void PathsModel::leave() {
  if (depth_ > 0) --depth_;
}

// A full pulse on the IRQ line, so a RISING handler can fire again later.
void PathsModel::pulseIrq() {
  if (port() == nullptr) return;
  port()->lineOut(kLineIrq, 1);
  port()->lineOut(kLineIrq, 0);
}

void PathsModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  statusFormat_ = port()->formatId("acme.stat.1", ebdev::schemaFingerprint("u8 hi,u8 lo"));
  commandFormat_ = port()->formatId("acme.cmd.1", ebdev::schemaFingerprint("u8 cmd"));
  resolved_ = true;
}

uint8_t PathsModel::i2cWrite(const uint8_t*, size_t, const ebdev::I2cTransfer&) {
  enter();
  leave();
  return ebdev::kI2cAck;
}

size_t PathsModel::i2cRead(uint8_t* data, size_t len,
                           const ebdev::I2cTransfer&) {
  enter();
  ++reads_;
  // Burst mode: raise more interrupts inside one read than the
  // environment can hold, to exercise its deferral-capacity contract.
  const uint8_t pulses = burst_;
  burst_ = 0;
  for (uint8_t i = 0; i < pulses; ++i) pulseIrq();
  size_t count = 0;
  if (len >= 2) {
    data[0] = 0x01;
    data[1] = 0x02;
    count = 2;
  }
  leave();
  return count;
}

void PathsModel::lineIn(uint8_t line, uint8_t level) {
  enter();
  if (line == kLineTrigger && level == 1) pulseIrq();
  leave();
}

void PathsModel::frameIn(uint8_t bus, uint16_t format, const uint8_t*,
                         size_t) {
  enter();
  resolve();
  if (bus == 0 && commandFormat_ != 0 && format == commandFormat_) {
    ++commands_;
    pulseIrq();
  }
  leave();
}

bool PathsModel::channelWrite(uint8_t channel, const uint8_t* data,
                              size_t len) {
  enter();
  bool applied = false;
  if (channel == kChannelBurst && len == 1) {
    burst_ = data[0];
    applied = true;
  }
  leave();
  return applied;
}

void PathsModel::advanceTo(uint64_t nowUs) {
  enter();
  resolve();
  if (!statusSent_ && nowUs >= 1000 && statusFormat_ != 0 && port() != nullptr) {
    statusSent_ = true;
    const uint8_t frame[2] = {0x00, 0x01};
    port()->frameOut(0, statusFormat_, frame, 16);
  }
  leave();
}

size_t PathsModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "paths reads=%u cmds=%u max_depth=%u", reads_,
                         commands_, maxDepth_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
