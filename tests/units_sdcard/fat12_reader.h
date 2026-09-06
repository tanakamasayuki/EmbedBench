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
  uint32_t totalSectors;
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
  // Everything below divides by these, so they are checked before they
  // are used rather than after. A volume claiming zero bytes per sector
  // passes the signature check and takes the arithmetic with it.
  if (v.bytesPerSector != UnitSdCardModel::kBlockSize) return false;
  if (v.sectorsPerCluster == 0 || v.fatCount == 0) return false;
  v.rootSector = v.reserved + v.fatCount * v.fatSectors;
  v.dataSector = v.rootSector + v.rootEntries * 32 / v.bytesPerSector;
  v.totalSectors = le16(scratch + 19);
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

// How a chain ended, which is the part worth asserting on: a reader that
// only reports "failed" cannot tell a looping volume from a short one.
enum ChainResult {
  kChainComplete,   // ran to the end-of-chain marker
  kChainLooped,     // came back to a cluster already visited
  kChainOutOfRange, // led somewhere the volume does not reach
  kChainShort,      // ended before the directory's size was satisfied
  kChainUnreadable, // a block the card would not give up
};

// Follow the cluster chain, reading up to `want` bytes into `out`. The
// visited-cluster guard is the whole point: without it a looping volume
// hangs the sketch, and a hang is the one failure a test cannot report.
inline ChainResult readChain(const Volume& v, uint16_t first, uint32_t want,
                             uint8_t* out, uint32_t& got, uint8_t* scratch) {
  const uint16_t kMaxClusters = 64;   // this volume can never need more
  bool seen[kMaxClusters] = {false};
  got = 0;
  uint16_t cluster = first;
  while (got < want) {
    if (cluster < 2 || cluster >= kMaxClusters) return kChainOutOfRange;
    if (seen[cluster]) return kChainLooped;
    seen[cluster] = true;

    const uint32_t sector =
        v.dataSector + (cluster - 2) * v.sectorsPerCluster;
    if (sector >= v.totalSectors) return kChainOutOfRange;
    if (!readBlock(sector, scratch)) return kChainUnreadable;
    uint32_t take = want - got;
    if (take > v.bytesPerSector) take = v.bytesPerSector;
    memcpy(out + got, scratch, take);
    got += take;
    if (got >= want) return kChainComplete;

    // The next link lives in the FAT, twelve bits per entry.
    const uint32_t entry = cluster * 3 / 2;
    if (!readBlock(v.reserved + entry / v.bytesPerSector, scratch)) {
      return kChainUnreadable;
    }
    const uint32_t at = entry % v.bytesPerSector;
    uint16_t next;
    if (cluster % 2 == 0) {
      next = static_cast<uint16_t>(scratch[at] | ((scratch[at + 1] & 0x0F) << 8));
    } else {
      next = static_cast<uint16_t>((scratch[at] >> 4) | (scratch[at + 1] << 4));
    }
    if (next >= 0xFF8) return kChainShort;  // end of chain, but not of file
    cluster = next;
  }
  return kChainComplete;
}
