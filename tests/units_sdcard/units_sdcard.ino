// A block device and a file system, on opposite sides of the boundary.
//
// The card offers numbered 512-byte blocks and nothing else. FAT12 — the
// boot parameters, the allocation table, the directory, the file — is
// built on top of that by the code below, which is the thing under test.
// That is the real division of labour on hardware too, and it is why the
// catalog has a block device rather than a file system.
#include <Arduino.h>
#include <EmbedBench.h>
#include <SPI.h>
#include <string.h>

#include <unit_sdcard_model.h>

#include "fat12_reader.h"

static UnitSdCardModel card;
static ebhost::DevicePort port;

static const uint8_t kPinCs = 5;

// [adapter begin]
static uint8_t spiTransfer(uint8_t mosi, void*) {
  return card.spiTransfer(mosi);
}
static void forwardPins(uint8_t pin, uint8_t value, void*) {
  if (pin == kPinCs) card.lineIn(UnitSdCardModel::kLineSelect, value);
}
static void advanceCard(uint64_t nowUs, void*) { card.advanceTo(nowUs); }
// [adapter end]

// --- The code under test: a minimal SD driver and FAT12 reader ---------------
static void select(bool on) { digitalWrite(kPinCs, on ? LOW : HIGH); }

static uint8_t command(uint8_t op, uint32_t arg) {
  const uint8_t packet[6] = {op,
                             static_cast<uint8_t>(arg >> 24),
                             static_cast<uint8_t>(arg >> 16),
                             static_cast<uint8_t>(arg >> 8),
                             static_cast<uint8_t>(arg),
                             0x95};
  for (size_t i = 0; i < sizeof(packet); ++i) SPI.transfer(packet[i]);
  // The reply follows the command; clock idle bytes until it arrives.
  for (int i = 0; i < 8; ++i) {
    const uint8_t r = SPI.transfer(0xFF);
    if (r != 0xFF) return r;
  }
  return 0xFF;
}

static bool cardBegin() {
  select(true);
  const uint8_t idle = command(UnitSdCardModel::kCmdGoIdle, 0);
  select(false);
  return idle == UnitSdCardModel::kR1Idle;
}

bool readBlock(uint32_t block, uint8_t* out) {
  select(true);
  if (command(UnitSdCardModel::kCmdReadSingle, block) !=
      UnitSdCardModel::kR1Ok) {
    select(false);
    return false;
  }
  // Wait for the data token, then take the block and its two CRC bytes.
  uint8_t token = 0xFF;
  for (int i = 0; i < 16 && token != UnitSdCardModel::kTokenStart; ++i) {
    token = SPI.transfer(0xFF);
  }
  if (token != UnitSdCardModel::kTokenStart) {
    select(false);
    return false;
  }
  for (size_t i = 0; i < UnitSdCardModel::kBlockSize; ++i) {
    out[i] = SPI.transfer(0xFF);
  }
  SPI.transfer(0xFF);
  SPI.transfer(0xFF);
  select(false);
  return true;
}

static bool writeBlock(uint32_t block, const uint8_t* data) {
  select(true);
  if (command(UnitSdCardModel::kCmdWriteSingle, block) !=
      UnitSdCardModel::kR1Ok) {
    select(false);
    return false;
  }
  SPI.transfer(UnitSdCardModel::kTokenStart);
  for (size_t i = 0; i < UnitSdCardModel::kBlockSize; ++i) {
    SPI.transfer(data[i]);
  }
  SPI.transfer(0xFF);
  const uint8_t accepted = SPI.transfer(0xFF);
  // The card holds the line at zero while it programs.
  for (int i = 0; i < 200 && SPI.transfer(0xFF) == 0x00; ++i) delayMicroseconds(50);
  select(false);
  return accepted == UnitSdCardModel::kDataAccepted;
}


static uint8_t scratch[UnitSdCardModel::kBlockSize];
static int mounted = -1;
static int found = -1;
static uint32_t fileSize = 0;
static char head[24] = {0};
static int badMount = -1;
static int emptyFound = -1;
static int zeroBpsMount = -1;
static uint8_t body[UnitSdCardModel::kBlockSize * 4];
static int chainOk = -1;
static uint32_t chainGot = 0;
static int chainLoop = -1;
static int chainRange = -1;
static int chainShort = -1;
static int wroteBack = -1;
static uint8_t reread = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_sdcard");
  SPI.begin(18, 19, 23, kPinCs);
  pinMode(kPinCs, OUTPUT);
  digitalWrite(kPinCs, HIGH);

  card.attach(&port);
  ebhost::bindSpiDevice(&spiTransfer);
  ebhost::setPinWriteForward(&forwardPins);
  ebhost::bindTickDevice(&advanceCard);
  card.reset();
  card.loadImage(ebsd::kFat12Hello);   // a real FAT12 volume, one file on it

  ebhost::runBegin(1000);

  Volume v;
  mounted = cardBegin() && mountVolume(v, scratch) ? 1 : 0;
  uint16_t cluster = 0;
  if (mounted == 1) {
    found = findFile(v, "HELLO   TXT", scratch, cluster, fileSize) ? 1 : 0;
  }
  if (found == 1) {
    const uint32_t sector = v.dataSector + (cluster - 2) * v.sectorsPerCluster;
    if (readBlock(sector, scratch)) {
      memcpy(head, scratch, sizeof(head) - 1);
    }
    // Write the file's cluster back with new content and read it again.
    memcpy(scratch, "REWRITTEN", 9);
    wroteBack = writeBlock(sector, scratch) ? 1 : 0;
    uint8_t after[UnitSdCardModel::kBlockSize];
    if (readBlock(sector, after)) reread = after[0];
  }

  // The other presets: a formatted volume with nothing on it, and one
  // whose boot signature is gone, which a driver has to refuse.
  card.loadImage(ebsd::kFat12Empty);
  Volume v2;
  if (mountVolume(v2, scratch)) {
    uint16_t c = 0;
    uint32_t sz = 0;
    emptyFound = findFile(v2, "HELLO   TXT", scratch, c, sz) ? 1 : 0;
  }
  card.loadImage(ebsd::kFat12BadBoot);
  Volume v3;
  badMount = mountVolume(v3, scratch) ? 1 : 0;

  // The volumes that are wrong in a way the first check cannot see. Each
  // one gets past the boot sector; what happens next is the point.
  struct Broken {
    const ebsd::Image* image;
    int* result;
  };
  // Zero bytes-per-sector: the signature is intact, so only checking the
  // divisors before using them saves the reader.
  card.loadImage(ebsd::kFat12ZeroBps);
  Volume v4;
  zeroBpsMount = mountVolume(v4, scratch) ? 1 : 0;

  // A chain that loops: without the visited guard this never returns.
  card.loadImage(ebsd::kFat12Circular);
  Volume v5;
  uint16_t c5 = 0;
  uint32_t sz5 = 0;
  if (mountVolume(v5, scratch) &&
      findFile(v5, "HELLO   TXT", scratch, c5, sz5)) {
    chainLoop = readChain(v5, c5, sz5, body, chainGot, scratch);
  }

  // A chain leading off the end of the volume.
  card.loadImage(ebsd::kFat12OutOfRange);
  Volume v6;
  uint16_t c6 = 0;
  uint32_t sz6 = 0;
  if (mountVolume(v6, scratch) &&
      findFile(v6, "HELLO   TXT", scratch, c6, sz6)) {
    uint32_t got = 0;
    chainRange = readChain(v6, c6, sz6, body, got, scratch);
  }

  // A directory entry claiming more than the chain holds.
  card.loadImage(ebsd::kFat12SizeMismatch);
  Volume v7;
  uint16_t c7 = 0;
  uint32_t sz7 = 0;
  if (mountVolume(v7, scratch) &&
      findFile(v7, "HELLO   TXT", scratch, c7, sz7)) {
    uint32_t got = 0;
    chainShort = readChain(v7, c7, sz7, body, got, scratch);
  }

  // And the good volume again, read through the same chain follower so
  // the healthy path is exercised by the same code.
  card.loadImage(ebsd::kFat12Hello);
  Volume v8;
  uint16_t c8 = 0;
  uint32_t sz8 = 0;
  if (mountVolume(v8, scratch) &&
      findFile(v8, "HELLO   TXT", scratch, c8, sz8)) {
    uint32_t got = 0;
    chainOk = readChain(v8, c8, sz8, body, got, scratch);
    chainGot = got;
  }

  char text[64];
  card.dump(text, sizeof(text));
  ebhost::dumpf("%s", text);
  ebhost::runEnd();

  static char trace[4096];
  ebhost::formatTrace(trace, sizeof(trace));
  Serial.printf("values mount=%d found=%d size=%u head=%.9s\n", mounted, found,
                fileSize, head);
  Serial.printf("values wrote=%d reread=%02X empty=%d bad=%d\n", wroteBack,
                reread, emptyFound, badMount);
  Serial.printf("values zerobps=%d ok=%d,%u loop=%d range=%d short=%d\n",
                zeroBpsMount, chainOk, chainGot, chainLoop, chainRange,
                chainShort);
  Serial.print(trace);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u folded=%u diag=%u\n", s.events,
                s.dropped, s.folded, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
