// What a fixed-capacity recorder should do when a polling-heavy scenario
// fills it (the open item from X50, where a status poll loop overflowed a
// 64-slot buffer and lost four events).
//
// The question is not "how big" — any fixed size can be filled — but what
// a test sees when it happens. A verification library must not let an
// assertion pass or fail for a reason the reader cannot see in the trace.
//
// Four policies over one scenario, plus the wake-slot table's behaviour
// when more devices want a wake than there are slots. Pure C++11.
#include <stdio.h>
#include <string.h>

#include <stdint.h>

namespace {

const size_t kSlots = 24;      // deliberately small: the scenario is 46
const size_t kTextMax = 40;

struct Record {
  uint32_t seq = 0;
  uint64_t timeUs = 0;
  uint64_t lastTimeUs = 0;
  uint32_t repeats = 1;        // 1 unless a cycle was folded into it
  char text[kTextMax] = {0};
};

// The scenario: what a real driver does to a part with a busy flag.
// A short setup, a poll loop that says the same thing over and over,
// then the conclusion — which is what the test actually asserts on.
struct Step {
  const char* text;
};

const size_t kSetup = 8;
const size_t kPollRounds = 16;
const size_t kPollWidth = 2;   // req + resp per round
const size_t kTail = 6;

size_t buildScenario(Record* out, size_t cap) {
  size_t n = 0;
  uint64_t t = 0;
  char text[kTextMax];
  for (size_t i = 0; i < kSetup && n < cap; ++i) {
    snprintf(text, sizeof(text), "setup step=%u", static_cast<unsigned>(i));
    out[n].seq = static_cast<uint32_t>(n + 1);
    out[n].timeUs = t;
    out[n].lastTimeUs = t;
    snprintf(out[n].text, kTextMax, "%s", text);
    ++n;
    t += 100;
  }
  for (size_t r = 0; r < kPollRounds && n < cap; ++r) {
    // Two different lines that repeat as a pair: adjacent events are
    // never equal, which is why collapsing neighbours does nothing here.
    const char* pair[kPollWidth] = {"i2c.req addr=76 data=F3",
                                    "i2c.rd.resp len=1 data=08"};
    for (size_t k = 0; k < kPollWidth && n < cap; ++k) {
      out[n].seq = static_cast<uint32_t>(n + 1);
      out[n].timeUs = t;
      out[n].lastTimeUs = t;
      snprintf(out[n].text, kTextMax, "%s", pair[k]);
      ++n;
      t += 500;
    }
  }
  const char* tail[kTail] = {"i2c.rd.resp len=1 data=00",
                             "gpio.inject pin=27 0->1",
                             "i2c.req addr=76 data=FA",
                             "i2c.rd.resp len=3 data=7F0000",
                             "dump env t=25 ready=1",
                             "run.end"};
  for (size_t i = 0; i < kTail && n < cap; ++i) {
    out[n].seq = static_cast<uint32_t>(n + 1);
    out[n].timeUs = t;
    out[n].lastTimeUs = t;
    snprintf(out[n].text, kTextMax, "%s", tail[i]);
    ++n;
    t += 100;
  }
  return n;
}

struct Result {
  size_t stored = 0;
  uint32_t firstSeq = 0;
  uint32_t lastSeq = 0;
  uint32_t lost = 0;
  bool tailPresent = false;   // did the conclusion survive?
  bool setupPresent = false;  // did the beginning survive?
  bool lossVisible = false;   // can a reader see it in the trace alone?
  uint32_t folded = 0;        // events represented by a repeat count
};

bool holds(const Record* buf, size_t n, const char* text) {
  for (size_t i = 0; i < n; ++i) {
    if (strcmp(buf[i].text, text) == 0) return true;
  }
  return false;
}

void finish(Result& r, const Record* buf, size_t n) {
  r.stored = n;
  r.firstSeq = n > 0 ? buf[0].seq : 0;
  r.lastSeq = n > 0 ? buf[n - 1].seq : 0;
  r.tailPresent = holds(buf, n, "run.end");
  r.setupPresent = holds(buf, n, "setup step=0");
  for (size_t i = 0; i < n; ++i) {
    if (buf[i].repeats > 1) r.folded += buf[i].repeats - 1;
  }
}

// A. What both environments do today: keep the first kSlots, count the
// rest. The count is honest but lives outside the trace.
Result dropNew(const Record* scene, size_t total) {
  Record buf[kSlots];
  size_t n = 0;
  Result r;
  for (size_t i = 0; i < total; ++i) {
    if (n < kSlots) {
      buf[n++] = scene[i];
    } else {
      ++r.lost;
    }
  }
  finish(r, buf, n);
  r.lossVisible = false;  // only the separate counter says so
  return r;
}

// B. A ring: the newest always fits, the oldest falls out.
Result dropOld(const Record* scene, size_t total) {
  Record buf[kSlots];
  size_t n = 0;
  Result r;
  for (size_t i = 0; i < total; ++i) {
    if (n < kSlots) {
      buf[n++] = scene[i];
    } else {
      for (size_t k = 1; k < kSlots; ++k) buf[k - 1] = buf[k];
      buf[kSlots - 1] = scene[i];
      ++r.lost;
    }
  }
  finish(r, buf, n);
  // A gap is visible: the first stored seq is not 1.
  r.lossVisible = n > 0 && buf[0].seq != 1;
  return r;
}

// C. Drop-new, but spend the last slot saying so, so the trace itself
// carries the gap instead of only a counter beside it.
Result dropNewMarked(const Record* scene, size_t total) {
  Record buf[kSlots];
  size_t n = 0;
  Result r;
  for (size_t i = 0; i < total; ++i) {
    if (n < kSlots - 1) {
      buf[n++] = scene[i];
    } else {
      ++r.lost;
    }
  }
  Record& mark = buf[n++];
  mark.seq = scene[total - 1].seq;
  mark.timeUs = scene[total - 1].timeUs;
  mark.lastTimeUs = mark.timeUs;
  snprintf(mark.text, kTextMax, "trace.truncated lost=%u",
           static_cast<unsigned>(r.lost));
  finish(r, buf, n);
  r.lossVisible = true;
  return r;
}

// D. Compact on full: rather than throw anything away, look for a cycle
// that is already in the buffer and fold its repetitions into one copy
// with a count and a time span. Nothing is discarded while a cycle can
// be found, and a buffer that never fills is left byte-for-byte alone —
// so existing traces do not move.
bool foldCycle(Record* buf, size_t& n) {
  // Try the longest plausible cycle first so a two-line poll pair is not
  // mistaken for two separate one-line cycles.
  for (size_t width = 8; width >= 1; --width) {
    if (n < width * 2) continue;
    for (size_t start = 0; start + width * 2 <= n; ++start) {
      bool same = true;
      for (size_t k = 0; k < width; ++k) {
        if (strcmp(buf[start + k].text, buf[start + width + k].text) != 0) {
          same = false;
          break;
        }
      }
      if (!same) continue;
      // Count how many further copies follow.
      size_t copies = 2;
      while (start + width * (copies + 1) <= n) {
        bool more = true;
        for (size_t k = 0; k < width; ++k) {
          if (strcmp(buf[start + k].text,
                     buf[start + width * copies + k].text) != 0) {
            more = false;
            break;
          }
        }
        if (!more) break;
        ++copies;
      }
      const size_t folded = width * (copies - 1);
      const uint64_t lastTime = buf[start + width * copies - 1].timeUs;
      for (size_t k = 0; k < width; ++k) {
        buf[start + k].repeats += static_cast<uint32_t>(copies - 1);
        buf[start + k].lastTimeUs = lastTime;
      }
      for (size_t k = start + width; k + folded < n; ++k) {
        buf[k] = buf[k + folded];
      }
      n -= folded;
      return true;
    }
  }
  return false;
}

Result compactOnFull(const Record* scene, size_t total) {
  Record buf[kSlots];
  size_t n = 0;
  Result r;
  for (size_t i = 0; i < total; ++i) {
    if (n >= kSlots && !foldCycle(buf, n)) {
      // Nothing repeats, so there is nothing to fold: degrade to keeping
      // the beginning and saying in the trace what was cut, which is the
      // marked policy. A silent truncation is the one outcome a
      // verification library must never produce.
      ++r.lost;
      continue;
    }
    buf[n++] = scene[i];
  }
  if (r.lost > 0) {
    if (n >= kSlots) --n;  // spend a slot on the notice
    Record& mark = buf[n++];
    mark.seq = scene[total - 1].seq;
    mark.timeUs = scene[total - 1].timeUs;
    mark.lastTimeUs = mark.timeUs;
    mark.repeats = 1;
    snprintf(mark.text, kTextMax, "trace.truncated lost=%u",
             static_cast<unsigned>(r.lost));
  }
  finish(r, buf, n);
  // A fold states its own count on the line; a truncation states itself.
  r.lossVisible = true;
  return r;
}

// The shape of an event: its text with every run of hex digits replaced
// by one placeholder, so `spi.req mosi=A0` and `spi.req mosi=A1` are the
// same shape while `uart.rx G` and `uart.rx P` are not.
void shapeOf(const char* text, char* out, size_t cap) {
  size_t o = 0;
  bool inDigits = false;
  for (size_t i = 0; text[i] != '\0' && o + 1 < cap; ++i) {
    const char c = text[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    if (hex) {
      if (!inDigits) {
        out[o++] = '*';
        inDigits = true;
      }
      continue;
    }
    inDigits = false;
    out[o++] = c;
  }
  out[o] = '\0';
}

uint8_t crc8Step(uint8_t crc, uint8_t value) {
  crc = static_cast<uint8_t>(crc ^ value);
  for (int i = 0; i < 8; ++i) {
    crc = static_cast<uint8_t>((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
  }
  return crc;
}

// The second tier: the same cycle search, but comparing shapes instead
// of exact text, so a request/response pair whose bytes all differ still
// folds. A cycle fold has nothing to work with there; the count and a
// checksum over the values still answer what a test asks — that it
// happened, how many times, and that the data was right. The values
// themselves are what is given up, which is why this runs only after the
// lossless tier has failed.
//
// Unlike the lossless tier it takes the candidate that frees the most
// slots rather than the first one found: a short setup whose steps are
// numbered is technically a cycle too, and folding that instead of the
// two-hundred-round burst would be the wrong trade.
bool foldShapeCycle(Record* buf, size_t& n) {
  char shapeA[kTextMax];
  char shapeB[kTextMax];
  size_t bestStart = 0;
  size_t bestWidth = 0;
  size_t bestCopies = 0;
  size_t bestRemoved = 0;
  for (size_t width = 1; width <= 8; ++width) {
    if (n < width * 2) continue;
    for (size_t start = 0; start + width * 2 <= n; ++start) {
      bool same = true;
      for (size_t k = 0; k < width && same; ++k) {
        shapeOf(buf[start + k].text, shapeA, sizeof(shapeA));
        shapeOf(buf[start + width + k].text, shapeB, sizeof(shapeB));
        if (strcmp(shapeA, shapeB) != 0) same = false;
      }
      if (!same) continue;
      size_t copies = 2;
      for (;;) {
        const size_t next = start + width * copies;
        if (next + width > n) break;
        bool more = true;
        for (size_t k = 0; k < width && more; ++k) {
          shapeOf(buf[start + k].text, shapeA, sizeof(shapeA));
          shapeOf(buf[next + k].text, shapeB, sizeof(shapeB));
          if (strcmp(shapeA, shapeB) != 0) more = false;
        }
        if (!more) break;
        ++copies;
      }
      const size_t removed = width * (copies - 1);
      if (removed > bestRemoved) {
        bestRemoved = removed;
        bestStart = start;
        bestWidth = width;
        bestCopies = copies;
      }
    }
  }
  if (bestRemoved == 0) return false;
  const uint64_t lastTime =
      buf[bestStart + bestWidth * bestCopies - 1].timeUs;
  for (size_t k = 0; k < bestWidth; ++k) {
    // The checksum covers this position across every copy, so a value
    // that changed is still detectable even though it is not printed.
    uint8_t crc = 0;
    uint32_t total = 0;
    for (size_t c = 0; c < bestCopies; ++c) {
      const Record& copy = buf[bestStart + bestWidth * c + k];
      total += copy.repeats;
      for (const char* q = copy.text; *q != '\0'; ++q) {
        crc = crc8Step(crc, static_cast<uint8_t>(*q));
      }
    }
    Record& head = buf[bestStart + k];
    shapeOf(head.text, shapeA, sizeof(shapeA));
    char summary[kTextMax * 2];
    snprintf(summary, sizeof(summary), "%s crc=%02X", shapeA, crc);
    size_t copy = strlen(summary);
    if (copy > kTextMax - 1) copy = kTextMax - 1;
    memcpy(head.text, summary, copy);
    head.text[copy] = '\0';
    head.repeats = total;
    head.lastTimeUs = lastTime;
  }
  for (size_t k = bestStart + bestWidth; k + bestRemoved < n; ++k) {
    buf[k] = buf[k + bestRemoved];
  }
  n -= bestRemoved;
  return true;
}

Result compactWithShapes(const Record* scene, size_t total) {
  Record buf[kSlots];
  size_t n = 0;
  Result r;
  for (size_t i = 0; i < total; ++i) {
    if (n >= kSlots && !foldCycle(buf, n) && !foldShapeCycle(buf, n)) {
      ++r.lost;
      continue;
    }
    buf[n++] = scene[i];
  }
  finish(r, buf, n);
  r.lossVisible = true;
  return r;
}

// A burst whose every value differs: the shape that defeats a cycle fold.
size_t buildBurst(Record* out, size_t rounds) {
  size_t n = 0;
  uint64_t t = 0;
  for (size_t i = 0; i < kSetup; ++i) {
    out[n].seq = static_cast<uint32_t>(n + 1);
    out[n].timeUs = t;
    out[n].lastTimeUs = t;
    out[n].repeats = 1;
    snprintf(out[n].text, kTextMax, "setup step=%u", static_cast<unsigned>(i));
    ++n;
    t += 100;
  }
  for (size_t i = 0; i < rounds; ++i) {
    const unsigned v = static_cast<unsigned>(i & 0xFF);
    out[n].seq = static_cast<uint32_t>(n + 1);
    out[n].timeUs = t;
    out[n].lastTimeUs = t;
    out[n].repeats = 1;
    snprintf(out[n].text, kTextMax, "spi.req mosi=%02X", v);
    ++n;
    out[n].seq = static_cast<uint32_t>(n + 1);
    out[n].timeUs = t;
    out[n].lastTimeUs = t;
    out[n].repeats = 1;
    snprintf(out[n].text, kTextMax, "spi.resp miso=%02X", 255u - v);
    ++n;
    t += 10;
  }
  out[n].seq = static_cast<uint32_t>(n + 1);
  out[n].timeUs = t;
  out[n].lastTimeUs = t;
  out[n].repeats = 1;
  snprintf(out[n].text, kTextMax, "run.end");
  ++n;
  return n;
}

// A scenario with nothing to fold, to see the degradation.
size_t buildDistinct(Record* out, size_t count) {
  uint64_t t = 0;
  for (size_t i = 0; i < count; ++i) {
    out[i].seq = static_cast<uint32_t>(i + 1);
    out[i].timeUs = t;
    out[i].lastTimeUs = t;
    out[i].repeats = 1;
    snprintf(out[i].text, kTextMax, "unique step=%u",
             static_cast<unsigned>(i));
    t += 100;
  }
  return count;
}

void report(const char* name, const Result& r) {
  printf("%s stored=%u first=%u last=%u lost=%u folded=%u setup=%u tail=%u "
         "visible=%u\n",
         name, static_cast<unsigned>(r.stored), r.firstSeq, r.lastSeq, r.lost,
         r.folded, r.setupPresent ? 1u : 0u, r.tailPresent ? 1u : 0u,
         r.lossVisible ? 1u : 0u);
}

// --- Wake slots -------------------------------------------------------------
// The other open capacity item: more devices wanting a wake than there
// are slots. Refusing is the only answer that keeps the interface's
// promise ("may advance more often than asked, never less") honest,
// because the device is told and can fall back to the boundaries it gets.
const size_t kWakeSlots = 8;

struct WakeTable {
  uint64_t at[kWakeSlots] = {0};
  uint32_t refused = 0;
  size_t highWater = 0;

  bool request(uint64_t whenUs) {
    for (size_t i = 0; i < kWakeSlots; ++i) {
      if (at[i] == whenUs) return true;  // same moment, one slot
    }
    for (size_t i = 0; i < kWakeSlots; ++i) {
      if (at[i] == 0) {
        at[i] = whenUs;
        size_t used = 0;
        for (size_t k = 0; k < kWakeSlots; ++k) {
          if (at[k] != 0) ++used;
        }
        if (used > highWater) highWater = used;
        return true;
      }
    }
    ++refused;
    return false;
  }
};

}  // namespace

int main() {
  printf("NATIVE start capacity_policy\n");

  Record scene[kSetup + kPollRounds * kPollWidth + kTail];
  const size_t total = buildScenario(scene, sizeof(scene) / sizeof(scene[0]));
  printf("scene events=%u slots=%u\n", static_cast<unsigned>(total),
         static_cast<unsigned>(kSlots));

  report("drop_new", dropNew(scene, total));
  report("drop_old", dropOld(scene, total));
  report("drop_marked", dropNewMarked(scene, total));
  report("compact", compactOnFull(scene, total));

  // The property that decides the blast radius: a run that fits is never
  // touched by the compacting policy, so pinned traces do not move.
  Record small[kSetup];
  const size_t fits = buildScenario(small, kSetup);
  const Result untouched = compactOnFull(small, fits);
  printf("compact_fits stored=%u lost=%u folded=%u\n",
         static_cast<unsigned>(untouched.stored), untouched.lost,
         untouched.folded);

  // A burst of 200 transfers whose every byte differs: no cycle exists,
  // so the run is summarised by shape instead — what happened, how many
  // times, and a checksum over the values.
  static Record burst[8 + 400 + 1];
  const size_t burstTotal = buildBurst(burst, 200);
  printf("burst events=%u\n", static_cast<unsigned>(burstTotal));
  report("burst_cycle_only", compactOnFull(burst, burstTotal));
  report("burst_shapes", compactWithShapes(burst, burstTotal));

  // Nothing repeats: the fold has no purchase and the policy must fall
  // back to something a reader can still see.
  Record distinct[46];
  const size_t distinctTotal = buildDistinct(distinct, 46);
  report("compact_nocycle", compactOnFull(distinct, distinctTotal));

  // Distinct moments beyond the table: each refusal is returned, none is
  // accepted and then quietly dropped.
  WakeTable wake;
  uint32_t accepted = 0;
  for (uint32_t i = 1; i <= 12; ++i) {
    if (wake.request(1000 + i * 100)) ++accepted;
  }
  printf("wake distinct=12 accepted=%u refused=%u high=%u\n", accepted,
         wake.refused, static_cast<unsigned>(wake.highWater));

  // Devices waiting for the same instant share one slot, so a broadcast
  // answered by many devices costs one slot, not one per device.
  WakeTable shared;
  uint32_t sharedAccepted = 0;
  for (uint32_t i = 0; i < 12; ++i) {
    if (shared.request(2000)) ++sharedAccepted;
  }
  printf("wake same=12 accepted=%u refused=%u high=%u\n", sharedAccepted,
         shared.refused, static_cast<unsigned>(shared.highWater));

  printf("NATIVE done\n");
  return 0;
}
