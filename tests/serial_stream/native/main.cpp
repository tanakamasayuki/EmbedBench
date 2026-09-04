// Native check of the serial byte-stream contract: the same command split
// across calls in every way yields the same reply, and two commands in one
// call yield two replies.
#include <stdio.h>

#include <modem_model.h>

namespace {

struct FakePort : public ebdev::HostPort {
  uint64_t now = 0;
  uint32_t outCalls = 0;
  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override {
    ++outCalls;
    return true;
  }
};

uint32_t replies(FakePort* port, AtModemModel* modem,
                 void (*feed)(AtModemModel*)) {
  modem->reset();
  port->now = 0;
  const uint32_t before = port->outCalls;
  feed(modem);
  port->now = 1000;
  modem->advanceTo(1000);
  return port->outCalls - before;
}

void whole(AtModemModel* m) {
  m->serialIn(reinterpret_cast<const uint8_t*>("AT+S;"), 5);
}
void bytes(AtModemModel* m) {
  const char* text = "AT+S;";
  for (int i = 0; i < 5; ++i) {
    m->serialIn(reinterpret_cast<const uint8_t*>(text + i), 1);
  }
}
void chunks(AtModemModel* m) {
  m->serialIn(reinterpret_cast<const uint8_t*>("AT"), 2);
  m->serialIn(reinterpret_cast<const uint8_t*>("+S;"), 3);
}
void two(AtModemModel* m) {
  m->serialIn(reinterpret_cast<const uint8_t*>("AT;AT;"), 6);
}

}  // namespace

int main() {
  printf("NATIVE start\n");
  FakePort port;
  AtModemModel modem;
  modem.attach(&port);
  printf("stream whole=%u bytes=%u chunks=%u two_in_one=%u\n",
         replies(&port, &modem, &whole), replies(&port, &modem, &bytes),
         replies(&port, &modem, &chunks), replies(&port, &modem, &two));
  printf("NATIVE done\n");
  return 0;
}
