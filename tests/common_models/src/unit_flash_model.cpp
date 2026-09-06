// SPI flash implementation. Pure C++11.
#include "unit_flash_model.h"

#include <stdio.h>

void UnitFlashModel::reset() {
  phase_ = kIdle;
  selected_ = false;
  command_ = 0;
  address_ = 0;
  writeEnabled_ = false;
  busy_ = false;
  doneUs_ = 0;
  for (size_t i = 0; i < kSize; ++i) {
    memory_[i] = 0xFF;  // erased
    staging_[i] = 0;
  }
  stagedLen_ = 0;
  for (size_t i = 0; i < kSize; ++i) page_[i] = 0;
  pageLen_ = 0;
  pageAddress_ = 0;
  programs_ = 0;
  refused_ = 0;
}

void UnitFlashModel::finishCommand() {
  if (command_ == kCmdPageProgram && stagedLen_ > 0) {
    if (!writeEnabled_) {
      // A real part drops this without a word. Saying so is the whole
      // point of running the part in a test instead of on a bench.
      ++refused_;
      if (port() != nullptr) port()->diagnose("program without write-enable");
    } else if (busy_) {
      ++refused_;
      if (port() != nullptr) port()->diagnose("program while busy");
    } else {
      busy_ = true;
      writeEnabled_ = false;  // one program per enable
      doneUs_ = (port() != nullptr ? port()->nowMicros() : 0) + kProgramUs;
      if (port() != nullptr) port()->requestWake(doneUs_);
      // The bytes have to outlive this command: they are committed when
      // the program time is up, and by then the application may well
      // have selected the part again to poll the status. Latching them
      // here is what a real page buffer does.
      pageAddress_ = address_;
      pageLen_ = stagedLen_;
      for (size_t i = 0; i < stagedLen_; ++i) page_[i] = staging_[i];
    }
  }
  phase_ = kIdle;
  command_ = 0;
  stagedLen_ = 0;
}

void UnitFlashModel::lineIn(uint8_t line, uint8_t level) {
  if (line != kLineSelect) return;
  const bool nowSelected = level == 0;  // active low
  if (nowSelected == selected_) return;
  selected_ = nowSelected;
  if (selected_) {
    phase_ = kCommand;
    command_ = 0;
    stagedLen_ = 0;
  } else {
    // Releasing the line is what commits a command, the way STOP does.
    finishCommand();
  }
}

uint8_t UnitFlashModel::spiTransfer(uint8_t mosi) {
  if (!selected_) return 0xFF;  // not addressed: the bus sees nothing
  switch (phase_) {
    case kCommand:
      command_ = mosi;
      if (command_ == kCmdWriteEnable) {
        writeEnabled_ = true;
        phase_ = kIdle;
      } else if (command_ == kCmdStatus) {
        phase_ = kData;
      } else {
        phase_ = kAddress;
      }
      return 0xFF;
    case kAddress:
      address_ = mosi;
      phase_ = kData;
      return 0xFF;
    case kData:
      if (command_ == kCmdStatus) {
        return static_cast<uint8_t>((busy_ ? kStatusBusy : 0) |
                                    (writeEnabled_ ? kStatusWriteEnabled : 0));
      }
      if (command_ == kCmdRead) {
        const uint8_t value =
            address_ < kSize ? memory_[address_] : 0xFF;
        if (address_ < kSize) ++address_;
        return value;
      }
      if (command_ == kCmdPageProgram) {
        // Held back until the line is released: nothing is committed
        // while the command could still be abandoned.
        if (stagedLen_ < kSize) staging_[stagedLen_++] = mosi;
        return 0xFF;
      }
      return 0xFF;
    default:
      return 0xFF;
  }
}

void UnitFlashModel::advanceTo(uint64_t nowUs) {
  if (!busy_ || nowUs < doneUs_) return;
  busy_ = false;
  for (size_t i = 0; i < pageLen_ && pageAddress_ + i < kSize; ++i) {
    memory_[pageAddress_ + i] = page_[i];
  }
  ++programs_;
  pageLen_ = 0;
}

size_t UnitFlashModel::channelRead(uint8_t channel, uint8_t* out, size_t cap) {
  if (channel != 0) return 0;
  size_t n = kSize;
  if (n > cap) n = cap;
  for (size_t i = 0; i < n; ++i) out[i] = memory_[i];
  return kSize;
}

size_t UnitFlashModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap,
                         "flash we=%u busy=%u progs=%u no=%u m0=%02X m1=%02X",
                         writeEnabled_ ? 1u : 0u, busy_ ? 1u : 0u, programs_,
                         refused_, memory_[0], memory_[1]);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
