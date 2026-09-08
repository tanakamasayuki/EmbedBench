// X64, step 1: the hand-written models through the shared sequences — the
// stand-in for a capture — for the environmental sensor (I2C) and the AT
// modem (serial). A fresh Session per run, as in capture_scaffold.
#include <stdio.h>

#include <env_sensor_model.h>
#include <modem_model.h>
#include <scenarios.h>

static void run(Scenario scenario, ebdev::Device& dev, const char* name) {
  Session* s = new Session();
  scenario(*s, dev);
  s->print("capture", name);
  delete s;
}

int main() {
  printf("CAPTURE start\n");
  EnvSensorModel env;
  run(&scenarioEnv, env, "env");
  AtModemModel modem;
  run(&scenarioModem, modem, "modem");
  printf("CAPTURE done\n");
  return 0;
}
