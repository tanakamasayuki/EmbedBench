// Native check that the all-paths model compiles without a platform and
// stays at depth 1 under a port that makes no callbacks.
#include <stdio.h>
#include <string.h>

#include "../paths_model.h"

namespace {

struct QuietPort : public ebdev::HostPort {
  uint32_t pulses = 0;
  uint32_t frames = 0;
  uint64_t nowMicros() override { return 1000; }
  void lineOut(uint8_t, uint8_t level) override {
    if (level == 1) ++pulses;
  }
  bool serialOut(const uint8_t*, size_t) override { return true; }
  uint16_t formatId(const char* name, uint32_t) override {
    return strcmp(name, "acme.stat.1") == 0 ? 1 : 2;
  }
  uint32_t maxFrameBits(uint8_t) override { return 64; }
  bool frameOut(uint8_t, uint16_t, const uint8_t*, size_t) override {
    ++frames;
    return true;
  }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  QuietPort port;
  PathsModel model;
  model.attach(&port);
  model.reset();
  const ebdev::I2cTransfer plain = {true, false};
  uint8_t buf[2];
  model.lineIn(PathsModel::kLineTrigger, 1);
  model.frameIn(0, 2, nullptr, 0);
  model.advanceTo(1000);
  const uint8_t burst[1] = {3};
  model.channelWrite(PathsModel::kChannelBurst, burst, 1);
  model.i2cRead(buf, 2, plain);
  char text[48];
  model.dump(text, sizeof(text));
  printf("paths pulses=%u frames=%u dump=<%s>\n", port.pulses, port.frames, text);
  printf("NATIVE done\n");
  return 0;
}
