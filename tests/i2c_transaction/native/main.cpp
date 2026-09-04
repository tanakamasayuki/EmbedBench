// Native check of the I2C transaction context: the same reads succeed
// under a repeated start and fail standalone, purely from I2cTransfer.
#include <stdio.h>

#include <regmap_model.h>

namespace {

struct NullPort : public ebdev::HostPort {
  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  NullPort port;
  RegisterMapModel model;
  model.attach(&port);
  model.reset();

  const ebdev::I2cTransfer openWrite = {false, false};   // write, no STOP
  const ebdev::I2cTransfer rsRead = {true, true};        // read after it
  const ebdev::I2cTransfer plain = {true, false};        // standalone
  const uint8_t pointer[1] = {0x01};
  uint8_t buf[1] = {0};

  model.i2cWrite(pointer, 1, openWrite);
  const size_t rsLen = model.i2cRead(buf, 1, rsRead);
  const uint8_t rsVal = buf[0];

  model.i2cWrite(pointer, 1, plain);
  buf[0] = 0;
  const size_t plainLen = model.i2cRead(buf, 1, plain);

  const uint8_t write[2] = {0x01, 0x77};
  model.i2cWrite(write, 2, plain);
  model.i2cWrite(pointer, 1, openWrite);
  const size_t afterLen = model.i2cRead(buf, 1, rsRead);

  char text[48];
  model.dump(text, sizeof(text));
  printf("regmap rs_len=%zu rs_val=%02X plain_len=%zu after_len=%zu after_val=%02X dump=<%s>\n",
         rsLen, rsVal, plainLen, afterLen, buf[0], text);
  printf("NATIVE done\n");
  return 0;
}
