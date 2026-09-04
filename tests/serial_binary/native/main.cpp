// Blocker check: a device reply containing NUL survives the interface, the
// environment's queue, and the environment's log. Driven through the
// native environment example so the real recorder is exercised.
#include <stdio.h>
#include <string.h>

#include <modem_model.h>
#include <nenv.h>

int main() {
  printf("NATIVE start\n");
  nenv::Env env;
  AtModemModel modem;
  modem.attach(&env);
  modem.reset();
  env.reset();
  env.bindSerial(&modem);

  // "AT+B;" asks for a three-byte reply whose middle byte is NUL.
  env.serialWrite(reinterpret_cast<const uint8_t*>("AT+B;"), 5);
  uint8_t reply[3] = {0xEE, 0xEE, 0xEE};
  const size_t got = env.serialRead(reply, sizeof(reply), 10000);
  printf("binary got=%zu bytes=%02X%02X%02X\n", got, reply[0], reply[1],
         reply[2]);

  static char trace[1024];
  env.formatTrace(trace, sizeof(trace));
  fputs(trace, stdout);
  printf("NATIVE done\n");
  return 0;
}
