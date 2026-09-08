// X64, step 3: the tapes the capture produced, played back — once under
// the recorded sequence, once under an application that strays from it.
#include <stdio.h>

#include <scenarios.h>

#include "env_tape.h"
#include "modem_tape.h"

static void run(Scenario scenario, ebdev::Device& dev, const char* variant,
                const char* name) {
  Session* s = new Session();
  scenario(*s, dev);
  s->print(variant, name);
  delete s;
}

int main() {
  printf("REPLAY start\n");
  EnvTape env;
  run(&scenarioEnv, env, "tape", "env");
  EnvTape envAgain;
  run(&scenarioEnvStrayed, envAgain, "strayed", "env");
  ModemTape modem;
  run(&scenarioModem, modem, "tape", "modem");
  printf("REPLAY done\n");
  return 0;
}
