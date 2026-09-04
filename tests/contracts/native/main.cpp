// Native check of the channel, dump, and time contracts against the
// shared reference models: channelWrite acceptance, snprintf-style
// channelRead / dump lengths and NUL termination, and advanceTo under
// repeats, jumps, and reset.
#include <stdio.h>
#include <string.h>

#include <modem_model.h>
#include <temp_model.h>

namespace {

struct FakePort : public ebdev::HostPort {
  uint64_t now = 0;
  uint32_t outCalls = 0;
  uint64_t outAt = 0;
  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t, uint8_t) override {}
  void serialOut(const uint8_t*, size_t) override {
    ++outCalls;
    outAt = now;
  }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  FakePort port;

  // --- channel / dump contracts ---------------------------------------------
  TempSensorModel temp;
  temp.attach(&port);
  temp.reset();
  const uint8_t raw300[2] = {0x01, 0x2C};
  const bool badChannel = temp.channelWrite(5, raw300, 2);
  const bool badLength = temp.channelWrite(0, raw300, 1);
  const bool ok = temp.channelWrite(0, raw300, 2);
  uint8_t out[2] = {0xEE, 0xEE};
  const size_t needCap1 = temp.channelRead(0, out, 1);
  const uint8_t firstByte = out[0];
  const uint8_t untouched = out[1];
  const size_t needCap2 = temp.channelRead(0, out, 2);
  const size_t badRead = temp.channelRead(9, out, 2);
  char small[8];
  memset(small, 'x', sizeof(small));
  const size_t dumpNeed = temp.dump(small, sizeof(small));
  printf("channel bad_chan=%d bad_len=%d ok=%d cap1_need=%zu b0=%02X b1=%02X "
         "cap2_need=%zu bad_read=%zu dump_need=%zu dump_out=<%s> dump_len=%zu\n",
         badChannel ? 1 : 0, badLength ? 1 : 0, ok ? 1 : 0, needCap1, firstByte,
         untouched, needCap2, badRead, dumpNeed, small, strlen(small));

  // --- time contract --------------------------------------------------------
  AtModemModel modem;
  modem.attach(&port);
  modem.reset();
  const uint8_t cmd[4] = {'A', 'T', '+', 'S'};
  port.now = 0;
  modem.serialIn(cmd, 4);  // due at 1000
  port.now = 1000;
  modem.advanceTo(1000);
  const uint32_t first = port.outCalls;
  modem.advanceTo(1000);  // same time again: must not emit twice
  const uint32_t repeat = port.outCalls;
  modem.serialIn(cmd, 4);  // due at 2000
  port.now = 5000;
  modem.advanceTo(5000);  // jump past the due time: emitted now
  const uint32_t jumpCalls = port.outCalls;
  const uint64_t jumpAt = port.outAt;
  modem.serialIn(cmd, 4);  // due at 6000, then dropped by reset
  modem.reset();
  port.now = 99999;
  modem.advanceTo(99999);
  printf("time first=%u repeat=%u jump_calls=%u jump_at=%llu after_reset=%u\n",
         first, repeat, jumpCalls, static_cast<unsigned long long>(jumpAt),
         port.outCalls);

  printf("NATIVE done\n");
  return 0;
}
