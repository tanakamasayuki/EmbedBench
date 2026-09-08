// Step 3 of X63: the same sequences against what the capture produced —
// first the generated table alone, then the table with the hooks a person
// wrote from its TODO list. A fresh Session per run, as in capture.cpp.
#include <stdio.h>

#include "hooks.h"
#include <scenarios.h>

static void run(Scenario scenario, ebdev::Device& dev, const char* variant,
                const char* name) {
  Session* s = new Session();
  scenario(*s, dev);
  s->print(variant, name);
  delete s;
}

int main() {
  printf("REPLAY start\n");
  TempTable tempTable;
  run(&scenarioTemp, tempTable, "table", "temp");
  TempHooked tempHooked;
  run(&scenarioTemp, tempHooked, "hooked", "temp");
  EnvTable envTable;
  run(&scenarioEnv, envTable, "table", "env");
  EnvHooked envHooked;
  run(&scenarioEnv, envHooked, "hooked", "env");
  ImuTable imuTable;
  run(&scenarioImu, imuTable, "table", "imu");
  ImuHooked imuHooked;
  run(&scenarioImu, imuHooked, "hooked", "imu");
  printf("REPLAY done\n");
  return 0;
}
