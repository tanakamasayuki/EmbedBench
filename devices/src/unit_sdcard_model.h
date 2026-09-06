// An SD card over SPI: the catalog's first block device, and the first
// model whose backing store is measured in kilobytes rather than bytes.
//
// The distinction that matters here is where the file system lives. A
// card offers numbered 512-byte blocks and nothing else; FAT, directory
// entries and file handles are all built on top of that by the library
// under test. So this model is a block device, and what a test asserts
// is that the application wrote the right bytes to the right block.
//
// Three things make it different from the SPI flash. Commands are six
// bytes with a response token rather than a single opcode. A transfer is
// 512 bytes, which is large enough that recording every byte is the
// wrong granularity. And the card answers 0xFF while it is busy, so the
// application polls — a wait with no upper bound if the model ever
// forgets to finish. Pure C++11.
#pragma once

#include <embedbench_device.h>

#include "sd_images.h"

class UnitSdCardModel : public ebdev::Device {
 public:
  static const size_t kBlockSize = 512;
  static const size_t kBlockCount = 8;   // 4 KB is enough to hold a FAT toy
  static const uint8_t kLineSelect = 0;  // active low

  // Commands, as they appear on the wire (0x40 | index).
  static const uint8_t kCmdGoIdle = 0x40;      // CMD0
  static const uint8_t kCmdSendIfCond = 0x48;  // CMD8
  static const uint8_t kCmdReadSingle = 0x51;  // CMD17
  static const uint8_t kCmdWriteSingle = 0x58; // CMD24

  // Response and data tokens.
  static const uint8_t kR1Idle = 0x01;
  static const uint8_t kR1Ok = 0x00;
  static const uint8_t kR1IllegalCommand = 0x04;
  static const uint8_t kTokenStart = 0xFE;
  static const uint8_t kDataAccepted = 0x05;

  // A single-block program is typically a few milliseconds (the spec
  // allows far longer worst cases). Physical for the typical case.
  static const uint64_t kWriteUs = 4000;

  // world: [block, fill] — put a known pattern in a block so a test can
  // tell what the application read.
  static const uint8_t kChannelFill = 0;
  static const uint8_t kChannelBlock = 1;  // read: the first 4 bytes

  // Put a whole volume on the card. The presets in sd_images.h are real
  // FAT12 images, so the code under test can be an actual file system
  // driver rather than something written to suit the model. Blocks the
  // image does not cover are zeroed. Returns false if it does not fit.
  bool loadImage(const ebsd::Image& image);
  // Raw block data, for an image of your own: `data` is `len` bytes
  // starting at block `firstBlock`, laid down exactly as given.
  bool loadBlocks(uint32_t firstBlock, const uint8_t* data, size_t len);

  void reset() override;
  void lineIn(uint8_t line, uint8_t level) override;
  uint8_t spiTransfer(uint8_t mosi) override;
  void advanceTo(uint64_t nowUs) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

 private:
  enum Phase {
    kIdlePhase,    // between commands
    kCommandPhase, // collecting the six command bytes
    kReplyPhase,   // handing back the R1 the command earned
    kReadPhase,    // sending the token then the block
    kWriteWait,    // waiting for the application's start token
    kWritePhase,   // collecting the block
    kBusyPhase,    // programming; answers 0 until done
  };

  void beginCommand();

  Phase phase_ = kIdlePhase;
  Phase after_ = kCommandPhase;  // where the R1 hands control to next
  bool selected_ = false;
  bool initialised_ = false;
  uint8_t command_[6] = {0};
  size_t commandLen_ = 0;
  uint32_t block_ = 0;
  size_t offset_ = 0;
  uint8_t pending_ = 0;      // the byte to answer next
  uint64_t doneUs_ = 0;
  uint8_t store_[kBlockCount][kBlockSize] = {{0}};
  uint8_t staging_[kBlockSize] = {0};
  uint32_t reads_ = 0;
  uint32_t writes_ = 0;
  uint32_t rejected_ = 0;
};
