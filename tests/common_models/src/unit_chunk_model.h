// A chunking node: the model that shows where a splitting rule belongs
// when one link carries less per frame than another.
//
// The interface deliberately gives capacity per bus (`maxFrameBits(bus)`)
// and refuses an oversized frame whole rather than splitting it, because
// splitting is a decision about meaning: which bytes may be separated,
// how the pieces are numbered, and how the far end knows the last one has
// arrived. Those belong to the format, and this model is the worked
// example of writing them down.
//
// Format `m5.chunk.1` is [seq, more, payload...]: `seq` counts from zero,
// `more` is 1 on every piece but the last. A link that cannot carry the
// two header bytes plus at least one payload byte cannot carry this
// format at all, and the model says so instead of emitting nothing.
//
// The pieces go out one per gap rather than all at once. A link with a
// limit on what fits in a frame has a cost per frame too, so a burst of
// ten chunks in no time at all was never a real thing — and modelling it
// that way overran the environment's deferral capacity, which is how the
// mistake was found (X56). Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitChunkModel : public ebdev::Device {
 public:
  static const size_t kHeaderBytes = 2;
  static const size_t kMaxMessage = 32;
  // world: [bus, payload...] — which link to send the message on.
  static const uint8_t kChannelSend = 0;
  static const uint8_t kChannelAssembled = 1;  // read back what arrived

  static const uint64_t kFrameGapUs = 500;  // time on the link per chunk

  void reset() override;
  void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,
               size_t bits) override;
  void advanceTo(uint64_t nowUs) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  void resolve();
  bool emitNext();

  uint16_t chunkFormat_ = 0;
  bool resolved_ = false;
  uint8_t assembled_[kMaxMessage] = {0};
  size_t assembledLen_ = 0;
  uint8_t expectSeq_ = 0;
  uint32_t sentFrames_ = 0;
  uint32_t sentMessages_ = 0;
  uint32_t received_ = 0;
  uint32_t refused_ = 0;
  uint32_t outOfOrder_ = 0;

  // The message being sent out, one chunk at a time.
  uint8_t pending_[kMaxMessage] = {0};
  size_t pendingLen_ = 0;
  size_t pendingSent_ = 0;
  size_t perFrame_ = 0;
  uint8_t sendBus_ = 0;
  uint8_t sendSeq_ = 0;
  uint64_t nextFrameUs_ = 0;
  bool sending_ = false;
};
