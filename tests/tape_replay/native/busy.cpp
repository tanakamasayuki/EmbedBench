// X67: a busy step holds the time a part was busy, not a poll count.
// The SHT30's high-repeatability measurement from a real capture, played
// back with polls at several periods. Built with the tape from
// trace2tape.py --busy (sht30_tape.h) or without it (SHT30_STEPWISE).
#include <stdio.h>

#include <embedbench_device.h>
#include <tape_model.h>

#include "sht30_tape.h"

namespace {

class ClockPort : public ebdev::HostPort {
 public:
  uint64_t now = 0;
  uint32_t diagnostics = 0;
  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool diagnose(const char* text) override {
    ++diagnostics;
    printf("diag %s\n", text);
    return true;
  }
};

// Write the measurement command, then poll a 6-byte read every `periodUs`
// until it answers, as a library does.
void measure(uint32_t periodUs) {
  ClockPort port;
  Sht30Tape tape;
  tape.attach(&port);
  tape.reset();
  const ebdev::I2cTransfer plain = {true, false};
  const uint8_t command[2] = {0x24, 0x00};
  const uint8_t status = tape.i2cWrite(command, sizeof(command), plain);
  uint8_t data[6] = {};
  size_t got = 0;
  unsigned polls = 0;
  while (got == 0 && polls < 100) {
    port.now += periodUs;
    ++polls;
    got = tape.i2cRead(data, sizeof(data), plain);
  }
  char dump[64];
  tape.dump(dump, sizeof(dump));
  printf("period=%u status=%u polls=%u answered_at=%u got=", periodUs, status,
         polls, static_cast<unsigned>(port.now));
  for (size_t i = 0; i < got; ++i) printf("%02X", data[i]);
  printf(" diag=%u dump=<%s>\n", port.diagnostics, dump);
}

}  // namespace

int main() {
  const uint32_t periods[] = {400, 1000, 3000};
  for (uint32_t p : periods) measure(p);
  return 0;
}
