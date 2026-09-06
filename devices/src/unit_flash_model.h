// A SPI flash: the first model whose protocol is locked by sequence.
// The same byte on the wire means different things depending on what
// came before, and a program that arrives without its write-enable is
// discarded — silently on real parts, which is exactly the bug worth
// catching, so this one says so.
//
// Chip select is a line, not a bus operation: it frames a command the
// way STOP frames an I2C transfer, and dropping it mid-command abandons
// that command. Pure C++11.
#pragma once

#include <embedbench_device.h>

class UnitFlashModel : public ebdev::Device {
 public:
  static const uint8_t kLineSelect = 0;  // active low, as on real parts
  static const uint8_t kCmdWriteEnable = 0x06;
  static const uint8_t kCmdPageProgram = 0x02;
  static const uint8_t kCmdRead = 0x03;
  static const uint8_t kCmdStatus = 0x05;
  static const uint8_t kCmdChipErase = 0xC7;
  static const uint8_t kStatusBusy = 0x01;
  static const uint8_t kStatusWriteEnabled = 0x02;
  static const size_t kSize = 64;
  // A page program on a serial flash is typically under a millisecond
  // and a few at worst. Physical.
  static const uint64_t kProgramUs = 3000;
  // COMPRESSED, heavily: a real chip erase takes seconds to tens of
  // seconds. 8,000 us keeps 'erase is much slower than program' as the
  // only property a test should rely on.
  static const uint64_t kEraseUs = 8000;

  // A part fresh out of the packet is erased. reset() is a power cycle,
  // not a new part: what was programmed survives it, because that is
  // what "power-on state" means for something non-volatile. Blanking it
  // again takes a chip erase, the same as on the bench.
  UnitFlashModel();

  void reset() override;
  void lineIn(uint8_t line, uint8_t level) override;
  uint8_t spiTransfer(uint8_t mosi) override;
  void advanceTo(uint64_t nowUs) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  enum Phase { kIdle, kCommand, kAddress, kData };
  enum Pending { kNothing, kProgramPending, kErasePending };

  void finishCommand();

  Phase phase_ = kIdle;
  bool selected_ = false;
  uint8_t command_ = 0;
  uint8_t address_ = 0;
  bool writeEnabled_ = false;
  bool busy_ = false;
  Pending pending_ = kNothing;
  uint64_t doneUs_ = 0;
  uint8_t memory_[kSize] = {0};
  uint8_t staging_[kSize] = {0};
  size_t stagedLen_ = 0;
  // The page buffer is latched when the program starts, so a status
  // command issued while the part is busy cannot disturb it.
  uint8_t page_[kSize] = {0};
  size_t pageLen_ = 0;
  uint8_t pageAddress_ = 0;
  uint32_t programs_ = 0;
  uint32_t refused_ = 0;
};
