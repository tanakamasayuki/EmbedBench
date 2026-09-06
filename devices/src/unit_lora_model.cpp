// LoRa unit implementation. Pure C++11.
#include "unit_lora_model.h"

#include <stdio.h>

void UnitLoraModel::reset() {
  uplinkFormat_ = 0;
  downlinkFormat_ = 0;
  resolved_ = false;
  busy_ = false;
  txDoneUs_ = 0;
  sent_ = 0;
  dropped_ = 0;
  received_ = 0;
  rssi_ = 0;
  snr_ = 0;
}

void UnitLoraModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  uplinkFormat_ = port()->formatId("m5.lora.up.1",
                                   ebdev::schemaFingerprint("u8 payload[]"));
  downlinkFormat_ = port()->formatId("m5.lora.dn.1",
                                     ebdev::schemaFingerprint("u8 payload[]"));
  resolved_ = true;
}

void UnitLoraModel::frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
                            size_t bits) {
  (void)data;
  resolve();
  if (bus != kBus || format != uplinkFormat_) return;
  // The radio has a payload limit of its own, separate from whatever the
  // environment will carry (maxFrameBits): a frame the transport accepts
  // can still be too long for this modem.
  if (ebdev::frameBytes(bits) > kMaxPayloadBytes) {
    ++dropped_;
    if (port() != nullptr) port()->diagnose("payload over radio limit");
    return;
  }
  if (busy_) {
    // Deaf while transmitting: the send is lost, not queued.
    ++dropped_;
    if (port() != nullptr) port()->diagnose("send while transmitting");
    return;
  }
  busy_ = true;
  if (port() != nullptr) {
    port()->lineOut(kLineTxDone, 0);
    // Air time grows with the payload — this is the whole point of the
    // radio, and the reason a send cannot be treated as instantaneous.
    txDoneUs_ = port()->nowMicros() +
                ebdev::frameBytes(bits) * kAirTimePerByteUs;
    port()->requestWake(txDoneUs_);
  }
  ++sent_;
}

void UnitLoraModel::advanceTo(uint64_t nowUs) {
  if (busy_ && nowUs >= txDoneUs_) {
    busy_ = false;
    if (port() != nullptr) port()->lineOut(kLineTxDone, 1);
  }
}

bool UnitLoraModel::channelWrite(uint8_t channel, const uint8_t* data,
                                 size_t len) {
  if (channel != kChannelDownlink || len < 2) return false;
  resolve();
  rssi_ = data[0];
  snr_ = data[1];
  const size_t payload = len - 2;
  if (payload > kMaxPayloadBytes) return false;
  ++received_;
  if (downlinkFormat_ != 0 && port() != nullptr) {
    port()->frameOut(kBus, downlinkFormat_, data + 2, payload * 8);
  }
  return true;
}

size_t UnitLoraModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  // Signal quality belongs to the reception, not to the payload, so the
  // application asks for it instead of finding it in the frame.
  if (channel != kChannelStatus || cap < 3) return 0;
  out[0] = rssi_;
  out[1] = snr_;
  out[2] = busy_ ? 1 : 0;
  return 3;
}

size_t UnitLoraModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "lora sent=%u dropped=%u rx=%u rssi=-%u busy=%u",
                         sent_, dropped_, received_, rssi_, busy_ ? 1u : 0u);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
