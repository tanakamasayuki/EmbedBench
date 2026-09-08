// X65, step 1: sessions with more than one device on them, captured — two
// I2C parts on one bus, a SPI flash, and a bus plus a serial port at once.
#include <stdio.h>

#include <env_sensor_model.h>
#include <modem_model.h>
#include <scenarios.h>
#include <temp_model.h>
#include <unit_flash_model.h>

int main() {
  printf("CAPTURE start\n");
  {
    Session* s = new Session();
    TempSensorModel temp;
    EnvSensorModel env;
    scenarioTwoDevices(*s, temp, env);
    s->print("capture", "two");
    delete s;
  }
  {
    Session* s = new Session();
    UnitFlashModel flash;
    scenarioFlash(*s, flash);
    s->print("capture", "flash");
    delete s;
  }
  {
    Session* s = new Session();
    EnvSensorModel env;
    AtModemModel modem;
    scenarioEnvAndModem(*s, env, modem);
    s->print("capture", "mixed");
    delete s;
  }
  printf("CAPTURE done\n");
  return 0;
}
