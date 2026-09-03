// Measures an experiment-local event record: its in-memory size, two
// fixed-buffer overflow policies with detectable loss, and the byte cost
// plus generation time of three one-line serialization candidates.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostClock.h>
#include <stdio.h>
#include <string.h>

using namespace HostArduino;

struct Event {
  uint32_t seq = 0;
  uint64_t timeUs = 0;
  uint8_t ctx = 0;     // 0=main, 1=tick, 2=isr
  uint8_t origin = 0;  // 0=app, 1=director, 2=device, 3=diag
  uint16_t kind = 0;   // 0=gpio.write, 1=i2c.write
  uint16_t length = 0; // payload bytes used
  uint8_t data[8] = {0};
};

constexpr size_t kCapacity = 8;

// Policy 1: a full buffer drops new events and counts them, so the tail
// of the sequence is what goes missing.
struct DropNewBuffer {
  Event slots[kCapacity];
  size_t count = 0;
  uint32_t dropped = 0;
  uint32_t lastSeqOffered = 0;

  bool push(const Event& e) {
    lastSeqOffered = e.seq;
    if (count >= kCapacity) {
      ++dropped;
      return false;
    }
    slots[count++] = e;
    return true;
  }
};

// Policy 2: a full buffer overwrites the oldest event and counts it, so
// the head of the sequence is what goes missing.
struct OverwriteBuffer {
  Event slots[kCapacity];
  size_t count = 0;
  size_t head = 0;
  uint32_t overwritten = 0;
  uint32_t firstSeqOffered = 0;

  void push(const Event& e) {
    if (firstSeqOffered == 0) firstSeqOffered = e.seq;
    if (count < kCapacity) {
      slots[(head + count) % kCapacity] = e;
      ++count;
    } else {
      slots[head] = e;
      head = (head + 1) % kCapacity;
      ++overwritten;
    }
  }
  const Event& oldest() const { return slots[head]; }
  const Event& newest() const { return slots[(head + count - 1) % kCapacity]; }
};

// Serialization candidates for the same gpio.write event.
static size_t formatSeqFirst(char* out, size_t cap, const Event& e) {
  return snprintf(out, cap,
                  "%08u %016llu main app gpio.write pin=%u level=%u",
                  e.seq, static_cast<unsigned long long>(e.timeUs), e.data[0],
                  e.data[1]);
}

static size_t formatTimeFirst(char* out, size_t cap, const Event& e) {
  return snprintf(out, cap,
                  "%016llu %08u main app gpio.write pin=%u level=%u",
                  static_cast<unsigned long long>(e.timeUs), e.seq, e.data[0],
                  e.data[1]);
}

static size_t formatJson(char* out, size_t cap, const Event& e) {
  return snprintf(out, cap,
                  "{\"seq\":%u,\"time_us\":%llu,\"ctx\":\"main\","
                  "\"origin\":\"app\",\"event\":\"gpio.write\","
                  "\"pin\":%u,\"level\":%u}",
                  e.seq, static_cast<unsigned long long>(e.timeUs), e.data[0],
                  e.data[1]);
}

// The same three candidates for an i2c.write carrying a 2-byte payload.
static size_t formatSeqFirstI2c(char* out, size_t cap, const Event& e) {
  return snprintf(out, cap,
                  "%08u %016llu main app i2c.write addr=%02X len=%u data=%02X%02X",
                  e.seq, static_cast<unsigned long long>(e.timeUs), e.data[0],
                  e.length, e.data[1], e.data[2]);
}

static size_t formatJsonI2c(char* out, size_t cap, const Event& e) {
  return snprintf(out, cap,
                  "{\"seq\":%u,\"time_us\":%llu,\"ctx\":\"main\","
                  "\"origin\":\"app\",\"event\":\"i2c.write\","
                  "\"addr\":\"%02X\",\"len\":%u,\"data\":\"%02X%02X\"}",
                  e.seq, static_cast<unsigned long long>(e.timeUs), e.data[0],
                  e.length, e.data[1], e.data[2]);
}

static Event makeEvent(uint32_t seq) {
  Event e;
  e.seq = seq;
  e.timeUs = 123456;
  e.kind = 0;
  e.length = 2;
  e.data[0] = 5;  // pin
  e.data[1] = 1;  // level
  return e;
}

static uint64_t timeFormatLoop(size_t (*fmt)(char*, size_t, const Event&),
                               const Event& e, uint32_t iterations,
                               uint64_t* totalBytes) {
  char line[160];
  uint64_t bytes = 0;
  const uint64_t start = clockRealNowMicros();
  for (uint32_t i = 0; i < iterations; ++i) {
    bytes += fmt(line, sizeof(line), e);
  }
  const uint64_t elapsed = clockRealNowMicros() - start;
  *totalBytes = bytes;
  return elapsed;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start event_buffer");

  Serial.printf("event_bytes=%u capacity=%u buffer_bytes=%u\n",
                static_cast<unsigned>(sizeof(Event)),
                static_cast<unsigned>(kCapacity),
                static_cast<unsigned>(sizeof(Event) * kCapacity));

  // Offer 12 sequential events to both 8-slot policies. Loss must be
  // detectable from the counters plus the stored sequence range alone.
  static DropNewBuffer dropNew;
  static OverwriteBuffer ring;
  for (uint32_t seq = 1; seq <= 12; ++seq) {
    const Event e = makeEvent(seq);
    dropNew.push(e);
    ring.push(e);
  }
  Serial.printf("drop_new stored=%u first=%u last=%u dropped=%u offered_last=%u\n",
                static_cast<unsigned>(dropNew.count), dropNew.slots[0].seq,
                dropNew.slots[dropNew.count - 1].seq, dropNew.dropped,
                dropNew.lastSeqOffered);
  Serial.printf("overwrite stored=%u first=%u last=%u overwritten=%u offered_first=%u\n",
                static_cast<unsigned>(ring.count), ring.oldest().seq,
                ring.newest().seq, ring.overwritten, ring.firstSeqOffered);

  // Bytes per event for each candidate, same event content.
  const Event gpio = makeEvent(42);
  Event i2c = makeEvent(43);
  i2c.kind = 1;
  i2c.data[0] = 0x34;  // address
  i2c.data[1] = 0xAB;
  i2c.data[2] = 0xCD;
  char line[160];
  Serial.printf("gpio_bytes seq_first=%u time_first=%u json=%u\n",
                static_cast<unsigned>(formatSeqFirst(line, sizeof(line), gpio)),
                static_cast<unsigned>(formatTimeFirst(line, sizeof(line), gpio)),
                static_cast<unsigned>(formatJson(line, sizeof(line), gpio)));
  Serial.printf("i2c_bytes seq_first=%u json=%u\n",
                static_cast<unsigned>(formatSeqFirstI2c(line, sizeof(line), i2c)),
                static_cast<unsigned>(formatJsonI2c(line, sizeof(line), i2c)));

  // Real-time cost of generating 100,000 lines per candidate.
  const uint32_t kIterations = 100000;
  uint64_t bytesSeq = 0, bytesTime = 0, bytesJson = 0;
  const uint64_t usSeq = timeFormatLoop(&formatSeqFirst, gpio, kIterations, &bytesSeq);
  const uint64_t usTime = timeFormatLoop(&formatTimeFirst, gpio, kIterations, &bytesTime);
  const uint64_t usJson = timeFormatLoop(&formatJson, gpio, kIterations, &bytesJson);
  Serial.printf("gen_100k_us seq_first=%llu time_first=%llu json=%llu\n",
                static_cast<unsigned long long>(usSeq),
                static_cast<unsigned long long>(usTime),
                static_cast<unsigned long long>(usJson));
  Serial.printf("gen_100k_bytes seq_first=%llu time_first=%llu json=%llu\n",
                static_cast<unsigned long long>(bytesSeq),
                static_cast<unsigned long long>(bytesTime),
                static_cast<unsigned long long>(bytesJson));

  Serial.println("TEST done");
}

void loop() { delay(10); }
