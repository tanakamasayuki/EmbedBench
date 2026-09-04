// Native comparison of the two ways an environment can handle an IRQ
// raised from inside a device method: run the ISR at once (the device is
// re-entered, depth 2) or defer it until the device call has returned
// (depth stays 1). The interface contract requires the latter.
#include <stdio.h>

#include "../irq_model.h"

namespace {

struct MiniEnv : public ebdev::HostPort {
  bool deferIsr;
  IrqSensorModel* model = nullptr;
  int deviceDepth = 0;
  bool pending = false;
  uint32_t isrRuns = 0;

  explicit MiniEnv(bool defer) : deferIsr(defer) {}

  uint64_t nowMicros() override { return 0; }
  bool serialOut(const uint8_t*, size_t) override { return true; }
  void lineOut(uint8_t, uint8_t level) override {
    if (level == 0) return;
    if (deferIsr && deviceDepth > 0) {
      pending = true;  // delivered after the device call completes
    } else {
      isr();
    }
  }

  // The application ISR reads the same device again.
  void isr() {
    ++isrRuns;
    uint8_t buf[2];
    read(buf);
  }

  size_t read(uint8_t* buf) {
    const ebdev::I2cTransfer xfer = {true, false};
    ++deviceDepth;
    const size_t n = model->i2cRead(buf, 2, xfer);
    --deviceDepth;
    if (deviceDepth == 0 && pending) {
      pending = false;
      isr();
    }
    return n;
  }
};

void run(bool defer, const char* name) {
  MiniEnv env(defer);
  IrqSensorModel model;
  env.model = &model;
  model.attach(&env);
  model.reset();
  uint8_t buf[2];
  env.read(buf);
  char text[40];
  model.dump(text, sizeof(text));
  printf("%s isr_runs=%u max_depth=%u dump=<%s>\n", name, env.isrRuns,
         model.maxDepth(), text);
}

}  // namespace

int main() {
  printf("NATIVE start\n");
  run(false, "immediate");
  run(true, "deferred");
  printf("NATIVE done\n");
  return 0;
}
