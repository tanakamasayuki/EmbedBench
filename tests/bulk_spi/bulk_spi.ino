// Bulk-transfer recording granularity (the X10 / Gate F homework): SPI
// bytes inside a transaction coalesce into one summary event with count
// and checksums, while transfers outside a transaction keep the per-byte
// request/response pair — whose cost is demonstrated by overflowing the
// event buffer with a 100-byte burst.
#include <Arduino.h>
#include <EmbedBench.h>
#include <SPI.h>
#include <string.h>

static uint8_t devTransfer(uint8_t mosi, void*) {
  return static_cast<uint8_t>(~mosi);
}

static void runOnce(char* out, size_t cap) {
  ebhost::runBegin(1000);

  // Framebuffer-scale burst inside a transaction: one summary line.
  SPI.beginTransaction(SPISettings(8000000, SPI_MSBFIRST, SPI_MODE0));
  for (uint32_t i = 0; i < 256; ++i) {
    SPI.transfer(static_cast<uint8_t>(i));
  }
  SPI.endTransaction();

  // Outside a transaction the per-byte pair remains (X24 behavior).
  for (uint32_t i = 0; i < 4; ++i) {
    SPI.transfer(static_cast<uint8_t>(0xA0 + i));
  }

  // The same burst without a transaction: 200 record attempts against a
  // 64-slot buffer — the explosion the summary avoids.
  for (uint32_t i = 0; i < 100; ++i) {
    SPI.transfer(static_cast<uint8_t>(i));
  }

  ebhost::runEnd();
  ebhost::formatTrace(out, cap);
}

static char run1[4096];
static char run2[4096];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start bulk_spi");

  SPI.begin(18, 19, 23, 5);
  ebhost::bindSpiDevice(&devTransfer);

  runOnce(run1, sizeof(run1));
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u\n", s.events, s.dropped);
  // The head of the trace carries the whole story; print it.
  char head[640];
  size_t pos = 0;
  const char* p = run1;
  for (int line = 0; line < 10 && *p != '\0'; ++line) {
    while (*p != '\0' && *p != '\n' && pos < sizeof(head) - 2) {
      head[pos++] = *p++;
    }
    if (*p == '\n') {
      head[pos++] = *p++;
    }
  }
  head[pos] = '\0';
  Serial.print(head);
  // Per-byte bursts have no repeating cycle to fold, so this is the one
  // scenario that still has to cut the trace. The last line says so
  // rather than letting it end without explanation.
  const char* tail = run1;
  for (const char* q = run1; *q != '\0'; ++q) {
    if (*q == '\n' && *(q + 1) != '\0') tail = q + 1;
  }
  Serial.printf("last %s", tail);

  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);

  Serial.println("TEST done");
}

void loop() { delay(10); }
