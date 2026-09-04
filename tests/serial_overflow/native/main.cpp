// Blocker check: serialOut into a receive queue that cannot take the whole
// reply. Driven through the native environment example.
#include <stdio.h>

#include <nenv.h>

#include "../flood_model.h"

int main() {
  printf("NATIVE start\n");
  nenv::Env env;
  FloodModel model;
  model.attach(&env);
  model.reset();
  env.reset();
  env.bindSerial(&model);
  env.setRxCapacity(8);  // eight bytes of room for a twelve-byte reply

  env.serialWrite(reinterpret_cast<const uint8_t*>("go"), 2);

  // The accepted prefix reaches the application...
  uint8_t got[12] = {0};
  const size_t read = env.serialRead(got, 8, 0);
  // ...and nothing else is queued behind it.
  uint8_t extra[1] = {0};
  const size_t leftover = env.serialRead(extra, 1, 0);
  char text[40];
  model.dump(text, sizeof(text));
  printf("overflow read=%zu prefix=%.8s leftover=%zu dump=<%s>\n", read, got,
         leftover, text);

  static char trace[1024];
  env.formatTrace(trace, sizeof(trace));
  fputs(trace, stdout);
  printf("NATIVE done\n");
  return 0;
}
