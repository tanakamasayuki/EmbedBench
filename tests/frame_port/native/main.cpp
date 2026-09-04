// Native portability proof for the frame-path model: plain g++, no
// Arduino, no host core.
#include <stdio.h>
#include <string.h>

#include "../node_model.h"

namespace {

struct FakePort : public ebdev::HostPort {
  uint64_t now = 0;
  uint16_t lastFormat = 0;
  uint8_t lastFrame[4] = {0};
  size_t lastBits = 0;
  uint32_t frames = 0;

  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t, uint8_t) override {}
  void serialOut(const uint8_t*, size_t) override {}
  uint8_t lastBus = 0xFF;
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    lastBus = bus;
    lastFormat = format;
    lastBits = bits;
    const size_t bytes = (bits + 7) / 8;
    memcpy(lastFrame, data, bytes < sizeof(lastFrame) ? bytes : sizeof(lastFrame));
    ++frames;
    return true;
  }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  FakePort port;
  RemoteNodeModel node;
  node.attach(&port);
  node.reset();

  // A frame addressed to someone else is ignored entirely.
  const uint8_t foreign[2] = {0x05, 0x08};
  node.frameIn(0, RemoteNodeModel::kFormatCommand, foreign, 16);
  const uint32_t framesAfterForeign = port.frames;

  // Our own command: the telemetry reply comes only from advanceTo.
  const uint8_t own[2] = {0x04, 0x08};
  port.now = 0;
  node.frameIn(0, RemoteNodeModel::kFormatCommand, own, 16);
  node.advanceTo(999);
  const uint32_t framesAt999 = port.frames;
  node.advanceTo(1000);
  char text[40];
  node.dump(text, sizeof(text));
  printf("node foreign=%u at999=%u frames=%u bus=%u fmt=%u bits=%zu data=%02X%02X dump=<%s>\n",
         framesAfterForeign, framesAt999, port.frames, port.lastBus,
         port.lastFormat, port.lastBits, port.lastFrame[0], port.lastFrame[1],
         text);

  printf("NATIVE done\n");
  return 0;
}
