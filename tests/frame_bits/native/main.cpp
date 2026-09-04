// Native check of frame bit packing and atomicity: MSB-first decoding of
// 12-bit frames, environment-side rejection of dirty padding, the empty
// frame as a trigger, and an atomic 128-bit frame refused on a 64-bit
// port rather than split.
#include <stdio.h>
#include <string.h>

#include "../ir_model.h"

namespace {

struct EnvPort : public ebdev::HostPort {
  uint32_t rejectedPadding = 0;
  uint32_t refusedOversize = 0;
  uint32_t accepted = 0;

  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  uint16_t formatId(const char* name, uint32_t) override {
    return strcmp(name, "acme.ir.1") == 0 ? 1 : 2;
  }
  uint32_t maxFrameBits(uint8_t) override { return 64; }
  bool frameOut(uint8_t, uint16_t, const uint8_t*, size_t bits) override {
    if (bits > 64) {
      ++refusedOversize;
      return false;
    }
    ++accepted;
    return true;
  }

  // The environment validates padding before delivering to a device.
  void deliver(IrReceiverModel* model, const uint8_t* data, size_t bits) {
    if (bits > 0 && !ebdev::framePaddingClean(data, bits)) {
      ++rejectedPadding;
      return;
    }
    model->frameIn(0, 1, data, bits);
  }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  EnvPort env;
  IrReceiverModel model;
  model.attach(&env);
  model.reset();

  const uint8_t cmdAbc[2] = {0xAB, 0xC0};  // 0xABC, MSB-first, clean pad
  const uint8_t cmd123[2] = {0x12, 0x30};  // 0x123
  const uint8_t dirty[2] = {0xAB, 0xCF};   // non-zero padding bits
  env.deliver(&model, cmdAbc, 12);
  char afterAbc[48];
  model.dump(afterAbc, sizeof(afterAbc));
  env.deliver(&model, cmd123, 12);
  env.deliver(&model, dirty, 12);
  env.deliver(&model, nullptr, 0);  // empty frame = trigger
  model.channelWrite(IrReceiverModel::kChannelStatus, nullptr, 0);

  char text[48];
  model.dump(text, sizeof(text));
  printf("frames first=<%s> rejected_padding=%u refused_oversize=%u accepted=%u dump=<%s>\n",
         afterAbc, env.rejectedPadding, env.refusedOversize, env.accepted, text);
  printf("NATIVE done\n");
  return 0;
}
