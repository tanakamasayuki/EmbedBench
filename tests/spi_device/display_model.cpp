// SPI display-style composite model implementation. Pure C++11.
#include "display_model.h"

#include <stdio.h>

void SpiDisplayModel::reset() {
  dc_ = 0;
  command_ = 0;
  dataCount_ = 0;
  checksum_ = 0;
  busyUntilUs_ = 0;
}

void SpiDisplayModel::lineIn(uint8_t line, uint8_t level) {
  if (line == kLineDc) dc_ = level;
}

uint8_t SpiDisplayModel::spiTransfer(uint8_t mosi) {
  if (dc_ == 0) {
    // Command byte: starts a new phase; a refresh raises the busy line
    // and time (advanceTo) releases it.
    command_ = mosi;
    dataCount_ = 0;
    checksum_ = 0;
    if (mosi == 0xFF && port() != nullptr) {
      busyUntilUs_ = port()->nowMicros() + kRefreshUs;
      port()->lineOut(kLineBusy, 1);
    }
    return 0x00;
  }
  // Data byte for the current command; MISO answers a running checksum so
  // the response path is verifiable per byte.
  ++dataCount_;
  checksum_ = static_cast<uint8_t>(checksum_ + mosi);
  return checksum_;
}

void SpiDisplayModel::advanceTo(uint64_t nowUs) {
  if (busyUntilUs_ != 0 && nowUs >= busyUntilUs_) {
    busyUntilUs_ = 0;
    if (port() != nullptr) port()->lineOut(kLineBusy, 0);
  }
}

size_t SpiDisplayModel::dump(char* out, size_t cap) {
  const int n = snprintf(out, cap, "disp cmd=%02X n=%u sum=%02X busy=%u",
                         command_, dataCount_, checksum_,
                         busyUntilUs_ != 0 ? 1 : 0);
  return n > 0 ? static_cast<size_t>(n) : 0;
}
