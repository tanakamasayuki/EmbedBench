// Native portability proof for the composite SPI display model: plain
// g++, no Arduino, no host core. Built and run by test_spi_device.py.
#include <stdio.h>
#include <string.h>

#include "../display_model.h"

namespace {

struct FakePort : public ebdev::HostPort {
  uint64_t now = 0;
  uint8_t lastLine = 0xFF;
  uint8_t lastLevel = 0xFF;
  uint32_t lineCalls = 0;

  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t line, uint8_t level) override {
    lastLine = line;
    lastLevel = level;
    ++lineCalls;
  }
  bool serialOut(const uint8_t*, size_t) override { return true; }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  FakePort port;
  SpiDisplayModel display;
  display.attach(&port);
  display.reset();

  // Command then data bytes, qualified by the DC input line.
  display.lineIn(SpiDisplayModel::kLineDc, 0);
  const uint8_t ackCmd = display.spiTransfer(0x2C);
  display.lineIn(SpiDisplayModel::kLineDc, 1);
  const uint8_t sum1 = display.spiTransfer(0x10);
  const uint8_t sum2 = display.spiTransfer(0x20);
  char afterData[48];
  display.dump(afterData, sizeof(afterData));
  printf("data ack=%02X sums=%02X,%02X dump=<%s>\n", ackCmd, sum1, sum2,
         afterData);

  // Refresh raises the busy line; only advanceTo releases it.
  display.lineIn(SpiDisplayModel::kLineDc, 0);
  display.spiTransfer(0xFF);
  const uint32_t callsAfterRefresh = port.lineCalls;
  display.advanceTo(999);
  const uint32_t callsAt999 = port.lineCalls;
  display.advanceTo(1000);
  char afterRefresh[48];
  display.dump(afterRefresh, sizeof(afterRefresh));
  printf("busy raise=%u:%u calls_999=%u calls_1000=%u level=%u dump=<%s>\n",
         port.lastLine, callsAfterRefresh, callsAt999, port.lineCalls,
         port.lastLevel, afterRefresh);

  printf("NATIVE done\n");
  return 0;
}
