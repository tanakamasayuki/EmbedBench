// Format identity with schema fingerprints on the host core: the same
// name with the same schema is idempotent, the same name with a different
// schema is a diagnosed conflict, a new version is a new format, and a
// frame with format 0 is refused.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_draft.h>

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start format_schema");

  ebd::runBegin(1000);
  const uint16_t a = ebd::registerFormat("acme.cmd.1", 0xA1);
  const uint16_t b = ebd::registerFormat("acme.cmd.1", 0xA1);
  const uint16_t c = ebd::registerFormat("acme.cmd.1", 0xB2);  // conflict
  const uint16_t d = ebd::registerFormat("acme.cmd.2", 0xB2);  // new version
  const uint8_t byte[1] = {0x55};
  const bool noFormat = ebd::frameTx(ebd::Origin::kApp, 0, 0, byte, 8);
  const bool okFrame = ebd::frameTx(ebd::Origin::kApp, 0, a, byte, 8);
  ebd::runEnd();

  static char trace[512];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values a=%u b=%u c=%u d=%u noformat=%d ok=%d\n", a, b, c, d,
                noFormat ? 1 : 0, okFrame ? 1 : 0);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u diag=%u\n", s.events, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
