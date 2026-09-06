// Modbus RTU slave model implementation. Pure C++11.
#include "unit_modbus_model.h"

#include <stdio.h>

uint16_t UnitModbusModel::crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
                      : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

void UnitModbusModel::reset() {
  frameLength_ = 0;
  idleAtUs_ = 0;
  pending_ = false;
  for (size_t i = 0; i < kRegisterCount; ++i) {
    registers_[i] = static_cast<uint16_t>(0x1000 + i);
  }
  answered_ = 0;
  badCrc_ = 0;
  foreign_ = 0;
}

void UnitModbusModel::serialIn(const uint8_t* data, size_t len) {
  if (port() == nullptr) return;
  for (size_t i = 0; i < len; ++i) {
    if (frameLength_ < sizeof(frame_)) {
      frame_[frameLength_++] = data[i];
    }
  }
  // Framing is silence, not a terminator: the request ends when the line
  // has been quiet long enough, so ask to be woken then.
  idleAtUs_ = port()->nowMicros() + kFrameGapUs;
  pending_ = true;
  port()->requestWake(idleAtUs_);
}

void UnitModbusModel::advanceTo(uint64_t nowUs) {
  if (!pending_ || nowUs < idleAtUs_) return;
  pending_ = false;
  handleFrame();
  frameLength_ = 0;
}

void UnitModbusModel::handleFrame() {
  if (frameLength_ < 4 || port() == nullptr) return;
  const uint16_t given = static_cast<uint16_t>(
      frame_[frameLength_ - 2] | (frame_[frameLength_ - 1] << 8));
  if (given != crc16(frame_, frameLength_ - 2)) {
    // A corrupted frame is not answered — that is how the protocol works,
    // and a test would otherwise have no way to see that it happened.
    ++badCrc_;
    port()->diagnose("frame dropped: bad crc");
    return;
  }
  if (frame_[0] != kAddress) {
    ++foreign_;  // another slave's business; silence is correct here
    return;
  }
  if (frame_[1] != kFuncReadHolding || frameLength_ != 8) {
    port()->diagnose("unsupported function");
    return;
  }
  const uint16_t first = static_cast<uint16_t>((frame_[2] << 8) | frame_[3]);
  const uint16_t count = static_cast<uint16_t>((frame_[4] << 8) | frame_[5]);
  if (first + count > kRegisterCount || count == 0) {
    port()->diagnose("register out of range");
    return;
  }
  uint8_t reply[16];
  reply[0] = kAddress;
  reply[1] = kFuncReadHolding;
  reply[2] = static_cast<uint8_t>(count * 2);
  size_t pos = 3;
  for (uint16_t i = 0; i < count; ++i) {
    const uint16_t value = registers_[first + i];
    reply[pos++] = static_cast<uint8_t>(value >> 8);
    reply[pos++] = static_cast<uint8_t>(value & 0xFF);
  }
  const uint16_t crc = crc16(reply, pos);
  reply[pos++] = static_cast<uint8_t>(crc & 0xFF);
  reply[pos++] = static_cast<uint8_t>(crc >> 8);
  port()->serialOut(reply, pos);
  ++answered_;
}

bool UnitModbusModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  if (channel != kChannelRegister || len != 3) return false;
  if (data[0] >= kRegisterCount) return false;
  registers_[data[0]] = static_cast<uint16_t>((data[1] << 8) | data[2]);
  return true;
}

size_t UnitModbusModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "modbus answered=%u bad_crc=%u foreign=%u r0=%04X",
                         answered_, badCrc_, foreign_, registers_[0]);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
