// The standard conformance scenario on environment example #2. The same
// probe and the same sequence run on the host core in conformance.ino, so
// the two environments are compared by verdict, not by trace text.
#include <stdio.h>
#include <string.h>

#include <conformance_probe.h>
#include <nenv.h>

namespace {

// A deliberately broken environment: it re-enters the device from a
// HostPort call, lets time run backwards, and truncates an oversized
// frame instead of refusing it. The probe must catch all three, otherwise
// the conformance kit proves nothing.
struct BrokenEnv : public ebdev::HostPort {
  ConformanceProbe* probe = nullptr;
  bool reentered = false;

  uint64_t nowMicros() override { return 0; }  // behind the last advanceTo
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  uint16_t formatId(const char* name, uint32_t) override {
    return strlen(name) > ebdev::kFormatNameMaxLength ? 0 : 1;
  }
  uint32_t maxFrameBits(uint8_t) override { return 64; }
  bool frameOut(uint8_t, uint16_t, const uint8_t*, size_t) override {
    // Accepts everything, including frames past the limit (truncation).
    if (!reentered && probe != nullptr) {
      reentered = true;  // re-enter the device from inside its own call
      const ebdev::I2cTransfer plain = {true, false};
      const uint8_t byte[1] = {0x00};
      probe->i2cWrite(byte, 1, plain);
    }
    return true;
  }
};

void runBroken() {
  BrokenEnv broken;
  ConformanceProbe probe;
  broken.probe = &probe;
  probe.attach(&broken);
  probe.reset();
  probe.advanceTo(2000);
  probe.advanceTo(1000);  // time runs backwards
  const uint8_t go[1] = {0x01};
  probe.channelWrite(ConformanceProbe::kChannelProbePort, go, sizeof(go));
  printf("broken ok=%d violations=%u\n", probe.conforms() ? 1 : 0,
         probe.violations());
}

}  // namespace

int main() {
  printf("NATIVE start\n");
  nenv::Env env;
  ConformanceProbe probe;
  probe.attach(&env);
  probe.reset();
  env.reset();
  env.bindI2c(0x70, &probe);
  env.bindSerial(&probe);
  env.bindChannel(&probe);
  env.addTicking(&probe);

  // The standard scenario: every inbound path, time moving and repeating,
  // and one port probe.
  const uint8_t payload[2] = {0x11, 0x22};
  env.i2cWrite(0x70, payload, sizeof(payload));
  uint8_t reading[2] = {0};
  env.i2cRead(0x70, reading, sizeof(reading));
  env.serialWrite(payload, sizeof(payload));
  // Two tick boundaries, so the probe sees time move forward twice and
  // can judge monotonicity (one advance alone proves nothing).
  env.delayMicros(1000);
  env.delayMicros(1000);
  const uint8_t go[1] = {0x01};
  env.chanWrite(ConformanceProbe::kChannelProbePort, go, sizeof(go));

  char text[48];
  probe.dump(text, sizeof(text));
  printf("conformance ok=%d checks=%03X violations=%u dump=<%s>\n",
         probe.conforms() ? 1 : 0, probe.checks(), probe.violations(), text);
  // The same probe must reject an environment that breaks the contracts.
  runBroken();

  printf("NATIVE done\n");
  return 0;
}
