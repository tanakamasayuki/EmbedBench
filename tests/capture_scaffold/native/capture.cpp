// Step 1 of X63: run the hand-written catalog models through the shared
// sequences and print their traces — the stand-in for a capture taken on
// a real board. Each run gets a fresh Session: environment bindings and
// ticking registrations persist across env.reset(), as they do on a
// board, so reusing one across devices would tick a model that is gone.
#include <stdio.h>

#include <env_sensor_model.h>
#include <temp_model.h>
#include <unit_imu_model.h>

#include <scenarios.h>

static void run(Scenario scenario, ebdev::Device& dev, const char* name) {
  Session* s = new Session();
  scenario(*s, dev);
  s->print("capture", name);
  delete s;
}

int main() {
  printf("CAPTURE start\n");
  TempSensorModel temp;
  run(&scenarioTemp, temp, "temp");
  EnvSensorModel env;
  run(&scenarioEnv, env, "env");
  UnitImuModel imu;
  run(&scenarioImu, imu, "imu");
  printf("CAPTURE done\n");
  return 0;
}
