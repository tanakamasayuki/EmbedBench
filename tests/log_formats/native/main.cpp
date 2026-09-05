// WP-B2's remainder: the three one-line candidates compared on what X10
// left open — parse time and how a diff reads — at 100,000 events. Native
// C++ so generation and parsing are measured on the same footing; the diff
// side is counted by the test from the files written here.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

const uint32_t kEvents = 100000;

uint64_t nowUs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000ull + ts.tv_nsec / 1000;
}

// One event's fields, as X10 defined them.
struct Row {
  uint32_t seq;
  uint64_t timeUs;
  uint8_t pin;
  uint8_t level;
};

Row makeRow(uint32_t i, bool variant) {
  Row r;
  r.seq = i + 1;
  r.timeUs = static_cast<uint64_t>(i) * 1000ull;
  r.pin = 5;
  // The variant differs in one field of one event, so a diff has exactly
  // one line to find in a hundred thousand.
  r.level = (variant && i == kEvents / 2) ? 0 : 1;
  return r;
}

size_t formatSeqFirst(char* out, size_t cap, const Row& r) {
  return snprintf(out, cap, "%08u %016llu main app gpio.write pin=%u level=%u",
                  r.seq, static_cast<unsigned long long>(r.timeUs), r.pin,
                  r.level);
}
size_t formatTimeFirst(char* out, size_t cap, const Row& r) {
  return snprintf(out, cap, "%016llu %08u main app gpio.write pin=%u level=%u",
                  static_cast<unsigned long long>(r.timeUs), r.seq, r.pin,
                  r.level);
}
size_t formatJson(char* out, size_t cap, const Row& r) {
  return snprintf(out, cap,
                  "{\"seq\":%u,\"time_us\":%llu,\"ctx\":\"main\","
                  "\"origin\":\"app\",\"event\":\"gpio.write\","
                  "\"pin\":%u,\"level\":%u}",
                  r.seq, static_cast<unsigned long long>(r.timeUs), r.pin,
                  r.level);
}

// Fixed-width parsing: the fields sit at known offsets, so a parser reads
// them without scanning for separators.
bool parseFixed(const char* line, bool seqFirst, Row* out) {
  char* end = nullptr;
  const unsigned long first = strtoul(line, &end, 10);
  if (end == line) return false;
  const unsigned long long second = strtoull(end, &end, 10);
  const char* pin = strstr(end, "pin=");
  const char* level = strstr(end, "level=");
  if (pin == nullptr || level == nullptr) return false;
  out->seq = static_cast<uint32_t>(seqFirst ? first : second);
  out->timeUs = seqFirst ? second : first;
  out->pin = static_cast<uint8_t>(strtoul(pin + 4, nullptr, 10));
  out->level = static_cast<uint8_t>(strtoul(level + 6, nullptr, 10));
  return true;
}

// JSON parsing without a library: find each key, then read its value.
// A real parser would be slower still; this is the optimistic case.
bool parseJson(const char* line, Row* out) {
  const char* seq = strstr(line, "\"seq\":");
  const char* time = strstr(line, "\"time_us\":");
  const char* pin = strstr(line, "\"pin\":");
  const char* level = strstr(line, "\"level\":");
  if (!seq || !time || !pin || !level) return false;
  out->seq = static_cast<uint32_t>(strtoul(seq + 6, nullptr, 10));
  out->timeUs = strtoull(time + 10, nullptr, 10);
  out->pin = static_cast<uint8_t>(strtoul(pin + 6, nullptr, 10));
  out->level = static_cast<uint8_t>(strtoul(level + 8, nullptr, 10));
  return true;
}

struct Result {
  const char* name;
  uint64_t writeUs;
  uint64_t parseUs;
  uint64_t bytes;
  uint32_t parsed;
};

Result run(const char* name, const char* path, bool variant,
           size_t (*fmt)(char*, size_t, const Row&),
           bool (*parse)(const char*, Row*)) {
  Result result = {name, 0, 0, 0, 0};
  char line[160];

  FILE* f = fopen(path, "w");
  const uint64_t writeStart = nowUs();
  for (uint32_t i = 0; i < kEvents; ++i) {
    const Row r = makeRow(i, variant);
    const size_t len = fmt(line, sizeof(line), r);
    result.bytes += len + 1;
    fwrite(line, 1, len, f);
    fputc('\n', f);
  }
  fclose(f);
  result.writeUs = nowUs() - writeStart;

  f = fopen(path, "r");
  uint32_t checksum = 0;
  const uint64_t parseStart = nowUs();
  while (fgets(line, sizeof(line), f) != nullptr) {
    Row r;
    if (parse(line, &r)) {
      ++result.parsed;
      checksum += r.level;  // touch the value so nothing is optimized away
    }
  }
  result.parseUs = nowUs() - parseStart;
  fclose(f);
  if (checksum == 0xFFFFFFFF) printf("unreachable\n");
  return result;
}

bool parseSeqFirst(const char* line, Row* out) {
  return parseFixed(line, true, out);
}
bool parseTimeFirst(const char* line, Row* out) {
  return parseFixed(line, false, out);
}

}  // namespace

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  char paths[6][256];
  const char* names[3] = {"seq_first", "time_first", "json"};
  size_t (*fmts[3])(char*, size_t, const Row&) = {&formatSeqFirst,
                                                  &formatTimeFirst,
                                                  &formatJson};
  bool (*parsers[3])(const char*, Row*) = {&parseSeqFirst, &parseTimeFirst,
                                           &parseJson};

  printf("NATIVE start\n");
  for (int i = 0; i < 3; ++i) {
    snprintf(paths[i * 2], sizeof(paths[0]), "%s/%s.log", dir, names[i]);
    snprintf(paths[i * 2 + 1], sizeof(paths[0]), "%s/%s.variant.log", dir,
             names[i]);
    const Result base = run(names[i], paths[i * 2], false, fmts[i],
                            parsers[i]);
    run(names[i], paths[i * 2 + 1], true, fmts[i], parsers[i]);
    printf("format %s bytes=%llu write_us=%llu parse_us=%llu parsed=%u\n",
           base.name, static_cast<unsigned long long>(base.bytes),
           static_cast<unsigned long long>(base.writeUs),
           static_cast<unsigned long long>(base.parseUs), base.parsed);
  }
  printf("NATIVE done\n");
  return 0;
}
