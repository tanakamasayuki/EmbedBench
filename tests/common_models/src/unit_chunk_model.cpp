// Chunking node implementation. Pure C++11.
#include "unit_chunk_model.h"

#include <stdio.h>

void UnitChunkModel::reset() {
  chunkFormat_ = 0;
  resolved_ = false;
  for (size_t i = 0; i < kMaxMessage; ++i) assembled_[i] = 0;
  assembledLen_ = 0;
  expectSeq_ = 0;
  sentFrames_ = 0;
  sentMessages_ = 0;
  received_ = 0;
  refused_ = 0;
  outOfOrder_ = 0;
  for (size_t i = 0; i < kMaxMessage; ++i) pending_[i] = 0;
  pendingLen_ = 0;
  pendingSent_ = 0;
  perFrame_ = 0;
  sendBus_ = 0;
  sendSeq_ = 0;
  nextFrameUs_ = 0;
  sending_ = false;
}

void UnitChunkModel::resolve() {
  if (resolved_ || port() == nullptr) return;
  chunkFormat_ = port()->formatId(
      "m5.chunk.1", ebdev::schemaFingerprint("u8 seq,u8 more,u8 body[]"));
  resolved_ = true;
}

bool UnitChunkModel::channelWrite(uint8_t channel, const uint8_t* data,
                                  size_t len) {
  if (channel != kChannelSend || len < 2) return false;
  resolve();
  if (port() == nullptr || chunkFormat_ == 0) return false;
  const uint8_t bus = data[0];
  const uint8_t* body = data + 1;
  const size_t bodyLen = len - 1;

  // The capacity is a property of the link, so it is asked for per bus
  // rather than assumed once. Everything below is the format's rule.
  const uint32_t capacityBits = port()->maxFrameBits(bus);
  const size_t capacityBytes = capacityBits / 8;
  if (capacityBytes <= kHeaderBytes) {
    ++refused_;
    port()->diagnose("link too small for a chunk header");
    return false;
  }
  perFrame_ = capacityBytes - kHeaderBytes;
  if (bodyLen > kMaxMessage) return false;

  sendBus_ = bus;
  pendingLen_ = bodyLen;
  for (size_t i = 0; i < bodyLen; ++i) pending_[i] = body[i];
  pendingSent_ = 0;
  sendSeq_ = 0;
  sending_ = true;
  // The first chunk goes now; the rest follow one gap apart, because the
  // link takes time per frame.
  if (!emitNext()) return false;
  ++sentMessages_;
  return true;
}

bool UnitChunkModel::emitNext() {
  if (!sending_ || port() == nullptr) return false;
  size_t take = pendingLen_ - pendingSent_;
  if (take > perFrame_) take = perFrame_;
  uint8_t frame[kHeaderBytes + kMaxMessage];
  frame[0] = sendSeq_++;
  frame[1] = (pendingSent_ + take) < pendingLen_ ? 1 : 0;
  for (size_t i = 0; i < take; ++i) {
    frame[kHeaderBytes + i] = pending_[pendingSent_ + i];
  }
  if (!port()->frameOut(sendBus_, chunkFormat_, frame,
                        (kHeaderBytes + take) * 8)) {
    // A refusal here means the rule and the link disagree, which is a bug
    // in the rule rather than something to paper over.
    ++refused_;
    sending_ = false;
    port()->diagnose("link refused a chunk within its stated capacity");
    return false;
  }
  ++sentFrames_;
  pendingSent_ += take;
  if (pendingSent_ >= pendingLen_) {
    sending_ = false;
    return true;
  }
  nextFrameUs_ = port()->nowMicros() + kFrameGapUs;
  port()->requestWake(nextFrameUs_);
  return true;
}

void UnitChunkModel::advanceTo(uint64_t nowUs) {
  while (sending_ && nowUs >= nextFrameUs_) {
    if (!emitNext()) break;
  }
}

void UnitChunkModel::frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
                             size_t bits) {
  (void)bus;
  resolve();
  if (format != chunkFormat_ || bits < kHeaderBytes * 8) return;
  const size_t bodyLen = ebdev::frameBytes(bits) - kHeaderBytes;
  if (data[0] != expectSeq_) {
    // Numbering is what makes a gap visible; without it a lost piece
    // would quietly shorten the message.
    ++outOfOrder_;
    if (port() != nullptr) port()->diagnose("chunk out of order");
    expectSeq_ = 0;
    assembledLen_ = 0;
    return;
  }
  if (data[0] == 0) assembledLen_ = 0;
  for (size_t i = 0; i < bodyLen && assembledLen_ < kMaxMessage; ++i) {
    assembled_[assembledLen_++] = data[kHeaderBytes + i];
  }
  if (data[1] != 0) {
    ++expectSeq_;
  } else {
    expectSeq_ = 0;
    ++received_;
  }
}

size_t UnitChunkModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel != kChannelAssembled) return 0;
  size_t n = assembledLen_;
  if (n > cap) n = cap;
  for (size_t i = 0; i < n; ++i) out[i] = assembled_[i];
  return assembledLen_;
}

size_t UnitChunkModel::dump(char* out, size_t cap) {
  // Kept short: an event's text field is narrower than a dump buffer.
  const int n = snprintf(out, cap,
                         "chunk sent=%u fr=%u rx=%u len=%u no=%u bad=%u",
                         sentMessages_, sentFrames_, received_,
                         static_cast<unsigned>(assembledLen_), refused_,
                         outOfOrder_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
