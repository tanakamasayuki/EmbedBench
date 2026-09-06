// SD card block device implementation. Pure C++11.
#include "unit_sdcard_model.h"

#include <stdio.h>

void UnitSdCardModel::reset() {
  phase_ = kIdlePhase;
  after_ = kCommandPhase;
  selected_ = false;
  initialised_ = false;
  for (size_t i = 0; i < sizeof(command_); ++i) command_[i] = 0;
  commandLen_ = 0;
  block_ = 0;
  offset_ = 0;
  pending_ = 0xFF;
  doneUs_ = 0;
  for (size_t i = 0; i < kBlockSize; ++i) staging_[i] = 0;
  reads_ = 0;
  writes_ = 0;
  rejected_ = 0;
  // The card is not the storage medium's owner in the way a flash chip
  // is: a real card keeps its blocks across a power cycle, so reset()
  // leaves store_ alone (see the same rule in unit_flash_model).
}

bool UnitSdCardModel::loadBlocks(uint32_t firstBlock, const uint8_t* data,
                                 size_t len) {
  if (data == nullptr) return false;
  const size_t start = static_cast<size_t>(firstBlock) * kBlockSize;
  if (start + len > kBlockCount * kBlockSize) return false;
  uint8_t* flat = &store_[0][0];
  for (size_t i = 0; i < len; ++i) flat[start + i] = data[i];
  return true;
}

bool UnitSdCardModel::loadImage(const ebsd::Image& image) {
  if (image.blocks > kBlockCount) return false;
  // The image is stored as its non-zero runs, so start from a blank card
  // and lay them down; everything the image does not mention is zero,
  // which is what an unwritten sector holds.
  uint8_t* flat = &store_[0][0];
  for (size_t i = 0; i < kBlockCount * kBlockSize; ++i) flat[i] = 0;
  for (size_t r = 0; r < image.runCount; ++r) {
    const ebsd::ImageRun& run = image.runs[r];
    if (run.offset + run.length > kBlockCount * kBlockSize) return false;
    for (size_t i = 0; i < run.length; ++i) {
      flat[run.offset + i] = run.bytes[i];
    }
  }
  return true;
}

void UnitSdCardModel::lineIn(uint8_t line, uint8_t level) {
  if (line != kLineSelect) return;
  const bool now = level == 0;
  if (now == selected_) return;
  selected_ = now;
  if (!selected_) {
    // Deselecting abandons whatever was in progress, except a write
    // already committed to the card, which keeps running.
    if (phase_ != kBusyPhase) phase_ = kIdlePhase;
    commandLen_ = 0;
  } else if (phase_ != kBusyPhase) {
    phase_ = kCommandPhase;
    commandLen_ = 0;
  }
}

void UnitSdCardModel::beginCommand() {
  const uint8_t op = command_[0];
  block_ = (static_cast<uint32_t>(command_[1]) << 24) |
           (static_cast<uint32_t>(command_[2]) << 16) |
           (static_cast<uint32_t>(command_[3]) << 8) |
           static_cast<uint32_t>(command_[4]);
  commandLen_ = 0;
  switch (op) {
    case kCmdGoIdle:
      initialised_ = true;
      pending_ = kR1Idle;
      after_ = kCommandPhase;
      phase_ = kReplyPhase;
      return;
    case kCmdSendIfCond:
      pending_ = initialised_ ? kR1Ok : kR1IllegalCommand;
      after_ = kCommandPhase;
      phase_ = kReplyPhase;
      return;
    case kCmdReadSingle:
    case kCmdWriteSingle:
      if (!initialised_) {
        // A card that was never reset into SPI mode does not answer
        // commands. Real ones simply return 0xFF forever, which reads as
        // a hang; saying so is the point of running it here.
        ++rejected_;
        pending_ = kR1IllegalCommand;
        after_ = kCommandPhase;
        phase_ = kReplyPhase;
        if (port() != nullptr) port()->diagnose("command before CMD0");
        return;
      }
      if (block_ >= kBlockCount) {
        ++rejected_;
        pending_ = kR1IllegalCommand;
        after_ = kCommandPhase;
        phase_ = kReplyPhase;
        if (port() != nullptr) port()->diagnose("block out of range");
        return;
      }
      pending_ = kR1Ok;
      offset_ = 0;
      after_ = op == kCmdReadSingle ? kReadPhase : kWriteWait;
      phase_ = kReplyPhase;
      return;
    default:
      ++rejected_;
      pending_ = kR1IllegalCommand;
      after_ = kCommandPhase;
      phase_ = kReplyPhase;
      if (port() != nullptr) port()->diagnose("unsupported command");
      return;
  }
}

uint8_t UnitSdCardModel::spiTransfer(uint8_t mosi) {
  if (!selected_) return 0xFF;
  switch (phase_) {
    case kCommandPhase: {
      if (mosi == 0xFF) return 0xFF;  // idle clocking, nothing to say yet
      command_[commandLen_++] = mosi;
      if (commandLen_ == sizeof(command_)) beginCommand();
      return 0xFF;
    }
    case kReplyPhase: {
      // The R1 comes back on the first idle byte after the command, and
      // only then does a read hand over its data token.
      const uint8_t reply = pending_;
      pending_ = 0xFF;
      offset_ = 0;
      phase_ = after_;
      return reply;
    }
    case kReadPhase: {
      if (offset_ == 0) {
        // One start token, then the block. Nothing before the token,
        // which is what the application waits for.
        ++offset_;
        return kTokenStart;
      }
      const size_t index = offset_ - 1;
      if (index < kBlockSize) {
        ++offset_;
        return store_[block_][index];
      }
      // Two CRC bytes the model does not compute; the value is not what
      // is under test, and saying so is better than pretending.
      // Token, 512 data bytes and two CRC bytes: the transfer is over
      // once all of them have been clocked out.
      ++offset_;
      if (offset_ >= kBlockSize + 3) {
        ++reads_;
        phase_ = kCommandPhase;
        pending_ = 0xFF;
      }
      return 0xFF;
    }
    case kWriteWait:
      if (mosi == kTokenStart) {
        phase_ = kWritePhase;
        offset_ = 0;
      }
      return 0xFF;
    case kWritePhase: {
      if (offset_ < kBlockSize) {
        staging_[offset_++] = mosi;
        return 0xFF;
      }
      // The two CRC bytes go by, then the card accepts and goes busy.
      ++offset_;
      if (offset_ < kBlockSize + 2) return 0xFF;
      phase_ = kBusyPhase;
      doneUs_ = (port() != nullptr ? port()->nowMicros() : 0) + kWriteUs;
      if (port() != nullptr) port()->requestWake(doneUs_);
      return kDataAccepted;
    }
    case kBusyPhase:
      return 0x00;  // busy: the application polls until this goes nonzero
    default:
      return 0xFF;
  }
}

void UnitSdCardModel::advanceTo(uint64_t nowUs) {
  if (phase_ != kBusyPhase || nowUs < doneUs_) return;
  for (size_t i = 0; i < kBlockSize; ++i) store_[block_][i] = staging_[i];
  ++writes_;
  phase_ = selected_ ? kCommandPhase : kIdlePhase;
  pending_ = 0xFF;
}

bool UnitSdCardModel::channelWrite(uint8_t channel, const uint8_t* data,
                                   size_t len) {
  if (channel != kChannelFill || len < 2) return false;
  if (data[0] >= kBlockCount) return false;
  for (size_t i = 0; i < kBlockSize; ++i) {
    // A pattern rather than a constant, so a test can tell one block
    // from another and an off-by-one offset from a correct read.
    store_[data[0]][i] = static_cast<uint8_t>(data[1] + i);
  }
  block_ = data[0];
  return true;
}

size_t UnitSdCardModel::channelRead(uint8_t channel, uint8_t* out,
                                    size_t cap) {
  if (channel != kChannelBlock) return ebdev::kChannelUnsupported;
  if (cap < 4) return 4;
  for (size_t i = 0; i < 4; ++i) out[i] = store_[block_][i];
  return 4;
}

size_t UnitSdCardModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "sd init=%u blk=%u rd=%u wr=%u no=%u b0=%02X%02X",
                         initialised_ ? 1u : 0u, block_, reads_, writes_,
                         rejected_, store_[block_][0], store_[block_][1]);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
