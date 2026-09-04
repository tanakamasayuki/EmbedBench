// Blocker check: an over-long i2cRead() result is caught by the
// environment, not trusted. Driven through the native environment example.
#include <stdio.h>

#include <nenv.h>

#include "../badlen_model.h"

int main() {
  printf("NATIVE start\n");
  nenv::Env env;
  BadLengthModel model;
  model.attach(&env);
  model.reset();
  env.reset();
  env.bindI2c(0x60, &model);

  uint8_t buf[2] = {0x11, 0x22};
  const size_t got = env.i2cRead(0x60, buf, sizeof(buf));
  printf("badlen got=%zu buf=%02X%02X\n", got, buf[0], buf[1]);

  static char trace[512];
  env.formatTrace(trace, sizeof(trace));
  fputs(trace, stdout);
  printf("NATIVE done\n");
  return 0;
}
