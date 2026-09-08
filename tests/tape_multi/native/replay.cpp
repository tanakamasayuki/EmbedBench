// X65, step 3: one tape per device, several tapes per session.
#include <stdio.h>

#include <scenarios.h>

#include "flash_tape.h"
#include "mixed_env_tape.h"
#include "mixed_modem_tape.h"
#include "two_env_tape.h"
#include "two_temp_tape.h"

int main() {
  printf("REPLAY start\n");
  {
    Session* s = new Session();
    TwoTempTape temp;
    TwoEnvTape env;
    scenarioTwoDevices(*s, temp, env);
    s->print("tape", "two");
    delete s;
  }
  {
    Session* s = new Session();
    FlashTape flash;
    scenarioFlash(*s, flash);
    s->print("tape", "flash");
    delete s;
  }
  {
    Session* s = new Session();
    MixedEnvTape env;
    MixedModemTape modem;
    scenarioEnvAndModem(*s, env, modem);
    s->print("tape", "mixed");
    delete s;
  }
  printf("REPLAY done\n");
  return 0;
}
