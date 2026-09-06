// Two catalog devices put to work in environment example #2: an
// environmental sensor whose forced measurement takes 7.5 ms, and a GPS
// that emits on its own schedule. Between them they use every path the
// interface offers a device except SPI and frames.
#include <stdio.h>
#include <string.h>

#include <env_sensor_model.h>
#include <gps_model.h>
#include <nenv.h>

int main() {
  printf("NATIVE start\n");
  nenv::Env env;
  EnvSensorModel sensor;
  GpsModel gps;
  sensor.attach(&env);
  gps.attach(&env);
  sensor.reset();
  gps.reset();
  env.reset();
  env.bindI2c(0x76, &sensor);
  env.bindSerial(&gps);
  env.bindChannel(&sensor);
  env.addTicking(&sensor);
  env.addTicking(&gps);

  // The world sets a temperature, the application asks for a measurement.
  const uint8_t temp[3] = {0x7F, 0xE0, 0x00};
  env.chanWrite(EnvSensorModel::kChannelTemp, temp, sizeof(temp));
  const uint8_t chipIdPointer[1] = {EnvSensorModel::kRegChipId};
  env.i2cWrite(0x76, chipIdPointer, 1, false);
  uint8_t chipId[1] = {0};
  env.i2cRead(0x76, chipId, 1);
  const uint8_t forced[2] = {EnvSensorModel::kRegCtrl,
                             EnvSensorModel::kCmdForced};
  env.i2cWrite(0x76, forced, 2);

  // The measurement is not finished after one tick, and is after eight.
  env.delayMicros(1000);
  const uint8_t statusPointer[1] = {EnvSensorModel::kRegStatus};
  env.i2cWrite(0x76, statusPointer, 1, false);
  uint8_t statusEarly[1] = {0};
  env.i2cRead(0x76, statusEarly, 1);
  env.delayMicros(7000);
  env.i2cWrite(0x76, statusPointer, 1, false);
  uint8_t statusLate[1] = {0};
  env.i2cRead(0x76, statusLate, 1);

  const uint8_t tempPointer[1] = {EnvSensorModel::kRegTemp};
  env.i2cWrite(0x76, tempPointer, 1, false);
  uint8_t reading[3] = {0};
  const size_t readLen = env.i2cRead(0x76, reading, 3);

  // An unmapped register: the sensor has no way to refuse a read, so it
  // says so through the diagnostic path.
  const uint8_t badPointer[1] = {0x42};
  env.i2cWrite(0x76, badPointer, 1, false);
  uint8_t nothing[1] = {0};
  const size_t badLen = env.i2cRead(0x76, nothing, 1);

  char sensorDump[64];
  sensor.dump(sensorDump, sizeof(sensorDump));
  printf("sensor chip=%02X early=%02X late=%02X read_len=%zu data=%02X%02X%02X "
         "bad_len=%zu dump=<%s>\n",
         chipId[0], statusEarly[0], statusLate[0], readLen, reading[0],
         reading[1], reading[2], badLen, sensorDump);

  // The GPS runs on its own schedule once started.
  env.serialWrite(reinterpret_cast<const uint8_t*>("START\n"), 6);
  env.delayMicros(5000);
  uint8_t sentence[40] = {0};
  const size_t got = env.serialRead(sentence, 20, 0);
  env.serialWrite(reinterpret_cast<const uint8_t*>("HELLO\n"), 6);
  char gpsDump[64];
  gps.dump(gpsDump, sizeof(gpsDump));
  printf("gps first=%.20s got=%zu dump=<%s>\n", sentence, got, gpsDump);

  printf("NATIVE done\n");
  return 0;
}
