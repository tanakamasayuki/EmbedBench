// The code under test: a FAT12 reader small enough to fit in an
// experiment and real enough to be wrong if the volume is wrong. It sees
// nothing but numbered blocks, which is all a card offers — everything
// below is built on top of readBlock() by the application, exactly as it
// is on hardware.
#pragma once

#include <stdint.h>
#include <string.h>

#include <unit_sdcard_model.h>

// Supplied by the sketch: the block driver this reader sits on.
bool readBlock(uint32_t block, uint8_t* out);

// A FAT12 reader, small enough to fit here and real enough to be wrong
// if the volume is wrong.
struct Volume {
  uint16_t bytesPerSector;
  uint8_t sectorsPerCluster;
  uint16_t reserved;
  uint8_t fatCount;
  uint16_t rootEntries;
  uint16_t fatSectors;
  uint32_t rootSector;
  uint32_t dataSector;
};

inline uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline bool mountVolume(Volume& v, uint8_t* scratch) {
  if (!readBlock(0, scratch)) return false;
  if (scratch[510] != 0x55 || scratch[511] != 0xAA) return false;
  v.bytesPerSector = le16(scratch + 11);
  v.sectorsPerCluster = scratch[13];
  v.reserved = le16(scratch + 14);
  v.fatCount = scratch[16];
  v.rootEntries = le16(scratch + 17);
  v.fatSectors = le16(scratch + 22);
  if (v.bytesPerSector != UnitSdCardModel::kBlockSize) return false;
  v.rootSector = v.reserved + v.fatCount * v.fatSectors;
  v.dataSector = v.rootSector + v.rootEntries * 32 / v.bytesPerSector;
  return true;
}

inline bool findFile(const Volume& v, const char* name83, uint8_t* scratch,
                     uint16_t& cluster, uint32_t& size) {
  if (!readBlock(v.rootSector, scratch)) return false;
  for (uint16_t i = 0; i < v.rootEntries; ++i) {
    const uint8_t* e = scratch + i * 32;
    if (e[0] == 0x00) break;
    if (e[0] == 0xE5) continue;
    if (memcmp(e, name83, 11) != 0) continue;
    cluster = le16(e + 26);
    size = le32(e + 28);
    return true;
  }
  return false;
}
