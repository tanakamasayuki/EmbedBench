// Native portability proof: drives the same device models with plain g++
// and a fake port — no Arduino, no host core, no EmbedBench environment.
// Built and run by test_device_if.py; never compiled by arduino-cli
// (subdirectories other than src/ are not sketch sources).
#include <stdio.h>
#include <string.h>

#include "../modem_model.h"
#include "../temp_model.h"

namespace {

struct FakePort : public ebdev::HostPort {
  uint64_t now = 0;
  uint8_t lastLine = 0xFF;
  uint8_t lastLevel = 0xFF;
  uint64_t lineAt = 0;
  uint32_t lineCalls = 0;
  char lastOut[16] = {0};
  uint64_t outAt = 0;
  uint32_t outCalls = 0;

  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t line, uint8_t level) override {
    lastLine = line;
    lastLevel = level;
    lineAt = now;
    ++lineCalls;
  }
  void serialOut(const uint8_t* data, size_t len) override {
    const size_t n = len < sizeof(lastOut) - 1 ? len : sizeof(lastOut) - 1;
    memcpy(lastOut, data, n);
    lastOut[n] = '\0';
    outAt = now;
    ++outCalls;
  }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  FakePort port;

  // Register-map model: config write, channel injection raising DRDY,
  // pointer write, block read, dump.
  TempSensorModel temp;
  temp.attach(&port);
  temp.reset();
  const uint8_t config[2] = {0x01, 0x05};
  const uint8_t status = temp.i2cWrite(config, sizeof(config));
  const uint8_t raw250[2] = {0x00, 0xFA};
  temp.channelWrite(0, raw250, sizeof(raw250));
  const uint8_t pointer[1] = {0x00};
  temp.i2cWrite(pointer, sizeof(pointer));
  uint8_t reading[2] = {0};
  const size_t readLen = temp.i2cRead(reading, sizeof(reading));
  char tempDump[40];
  temp.dump(tempDump, sizeof(tempDump));
  printf("temp status=%u line=%u:%u line_calls=%u read_len=%zu read=%02X%02X dump=<%s>\n",
         status, port.lastLine, port.lastLevel, port.lineCalls, readLen,
         reading[0], reading[1], tempDump);

  // Command model: immediate reply, then a 1,000 us latency reply driven
  // only by advanceTo().
  AtModemModel modem;
  modem.attach(&port);
  modem.reset();
  port.now = 0;
  modem.serialIn(reinterpret_cast<const uint8_t*>("AT"), 2);
  printf("modem at=<%s> at_t=%llu\n", port.lastOut,
         static_cast<unsigned long long>(port.outAt));
  modem.serialIn(reinterpret_cast<const uint8_t*>("AT+S"), 4);
  port.now = 999;
  modem.advanceTo(port.now);
  const uint32_t callsAt999 = port.outCalls;
  port.now = 1000;
  modem.advanceTo(port.now);
  char modemDump[40];
  modem.dump(modemDump, sizeof(modemDump));
  printf("modem calls999=%u out=<%s> out_t=%llu dump=<%s>\n", callsAt999,
         port.lastOut, static_cast<unsigned long long>(port.outAt), modemDump);

  printf("NATIVE done\n");
  return 0;
}
