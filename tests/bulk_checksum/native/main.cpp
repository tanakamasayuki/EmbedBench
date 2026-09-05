// X28's open question: when a bulk transfer is recorded as a summary, what
// must the summary contain for the record to still catch a difference?
// Compares a byte sum against CRC-8 on the corruptions a device model
// actually produces — a changed byte, two bytes swapped, a lost byte, a
// duplicated byte — over realistic payloads.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

uint8_t byteSum(const uint8_t* data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; ++i) sum = static_cast<uint8_t>(sum + data[i]);
  return sum;
}

// CRC-8/ATM (polynomial 0x07), the smallest checksum that reacts to the
// order of the bytes as well as their values.
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

const size_t kLen = 256;

void fill(uint8_t* out, uint32_t seed) {
  // A framebuffer-ish payload: mostly a gradient, which is what makes
  // transpositions common and sums blind to them.
  for (size_t i = 0; i < kLen; ++i) {
    out[i] = static_cast<uint8_t>(seed + i * 3);
  }
}

struct Score {
  const char* name;
  uint32_t sumCaught;
  uint32_t crcCaught;
  uint32_t cases;
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  uint8_t base[kLen];
  uint8_t altered[kLen];

  Score scores[4] = {{"changed_byte", 0, 0, 0},
                     {"swapped_pair", 0, 0, 0},
                     {"lost_byte", 0, 0, 0},
                     {"duplicated_byte", 0, 0, 0}};

  for (uint32_t seed = 0; seed < 64; ++seed) {
    fill(base, seed);
    const uint8_t baseSum = byteSum(base, kLen);
    const uint8_t baseCrc = crc8(base, kLen);

    for (size_t i = 0; i + 1 < kLen; ++i) {
      // 1. One byte changed: both a sum and a CRC must notice.
      memcpy(altered, base, kLen);
      altered[i] = static_cast<uint8_t>(altered[i] ^ 0x01);
      ++scores[0].cases;
      if (byteSum(altered, kLen) != baseSum) ++scores[0].sumCaught;
      if (crc8(altered, kLen) != baseCrc) ++scores[0].crcCaught;

      // 2. Two adjacent bytes swapped: the sum is unchanged by
      // construction, so only an order-sensitive checksum sees it.
      memcpy(altered, base, kLen);
      const uint8_t tmp = altered[i];
      altered[i] = altered[i + 1];
      altered[i + 1] = tmp;
      ++scores[1].cases;
      if (byteSum(altered, kLen) != baseSum) ++scores[1].sumCaught;
      if (crc8(altered, kLen) != baseCrc) ++scores[1].crcCaught;

      // 3. A byte lost (the rest shifts up, one zero at the end) and
      // 4. a byte duplicated: length changes too, but the summary must
      // not depend on the length field alone to notice.
      memcpy(altered, base, kLen);
      memmove(altered + i, altered + i + 1, kLen - i - 1);
      altered[kLen - 1] = 0;
      ++scores[2].cases;
      if (byteSum(altered, kLen) != baseSum) ++scores[2].sumCaught;
      if (crc8(altered, kLen) != baseCrc) ++scores[2].crcCaught;

      memcpy(altered, base, kLen);
      memmove(altered + i + 1, altered + i, kLen - i - 1);
      ++scores[3].cases;
      if (byteSum(altered, kLen) != baseSum) ++scores[3].sumCaught;
      if (crc8(altered, kLen) != baseCrc) ++scores[3].crcCaught;
    }
  }

  for (int i = 0; i < 4; ++i) {
    printf("case %s cases=%u sum_caught=%u crc_caught=%u\n", scores[i].name,
           scores[i].cases, scores[i].sumCaught, scores[i].crcCaught);
  }

  // Cost per bulk record, so the choice is not made on detection alone.
  uint8_t sink = 0;
  const uint32_t kRounds = 20000;
  for (uint32_t i = 0; i < kRounds; ++i) sink = static_cast<uint8_t>(sink + byteSum(base, kLen));
  for (uint32_t i = 0; i < kRounds; ++i) sink = static_cast<uint8_t>(sink + crc8(base, kLen));
  printf("cost sink=%u rounds=%u len=%u\n", sink, kRounds,
         static_cast<unsigned>(kLen));
  printf("NATIVE done\n");
  return 0;
}
