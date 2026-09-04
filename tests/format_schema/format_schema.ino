// Format identity on the host core: same name + same schema is idempotent,
// same name + different schema is a diagnosed conflict, a new version is a
// new format, names up to 19 characters are kept whole while a 20-character
// name is refused (never truncated), and frames with format 0 or an
// unregistered id are refused.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_device.h>
#include <embedbench_draft.h>

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start format_schema");

  const uint32_t layoutA = ebdev::schemaFingerprint("u8 addr,u8 cmd");
  const uint32_t layoutB = ebdev::schemaFingerprint("u8 cmd,u8 addr");

  ebd::runBegin(1000);
  const uint16_t a = ebd::registerFormat("acme.cmd.1", layoutA);
  const uint16_t b = ebd::registerFormat("acme.cmd.1", layoutA);
  const uint16_t c = ebd::registerFormat("acme.cmd.1", layoutB);  // conflict
  const uint16_t d = ebd::registerFormat("acme.cmd.2", layoutB);  // new version
  const uint16_t e = ebd::registerFormat("acme.cmd.longnam.12", layoutA);  // 19
  const uint16_t f = ebd::registerFormat("acme.cmd.longnam.12", layoutA);
  const uint16_t g = ebd::registerFormat("acme.cmd.longname.12", layoutA);  // 20
  const uint8_t byte[1] = {0x55};
  const bool noFormat = ebd::frameTx(ebd::Origin::kApp, 0, 0, byte, 8);
  const bool unknown = ebd::frameTx(ebd::Origin::kApp, 0, 7, byte, 8);
  const bool okFrame = ebd::frameTx(ebd::Origin::kApp, 0, a, byte, 8);
  ebd::runEnd();

  static char trace[768];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values a=%u b=%u c=%u d=%u e=%u f=%u g=%u noformat=%d unknown=%d ok=%d\n",
                a, b, c, d, e, f, g, noFormat ? 1 : 0, unknown ? 1 : 0,
                okFrame ? 1 : 0);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u diag=%u\n", s.events, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
