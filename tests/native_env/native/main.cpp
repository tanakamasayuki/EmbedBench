// The device_if scenario (X23) replayed on environment example #2: the
// same unmodified temp_model / modem_model sources, driven by a native
// application through nenv::Env instead of Arduino APIs on the host core.
#include <stdio.h>
#include <string.h>

#include <modem_model.h>
#include <temp_model.h>

#include <nenv.h>

namespace {

nenv::Env env;
TempSensorModel temp;
AtModemModel modem;

struct Values {
  uint16_t t1 = 0;
  char reply[3] = {0};
  uint64_t elapsed = 0;
};

void runOnce(char* out, size_t cap, Values* values) {
  temp.reset();
  modem.reset();
  // Setup preset through the model itself, before the run window opens.
  const uint8_t raw250[2] = {0x00, 0xFA};
  temp.channelWrite(0, raw250, 2);
  env.reset();

  // Application: configure the sensor.
  const uint8_t config[2] = {0x01, 0x05};
  env.i2cWrite(0x48, config, 2);

  // Director: new sample arrives; the device raises DRDY through its port.
  const uint8_t raw300[2] = {0x01, 0x2C};
  env.chanWrite(0, raw300, 2);

  // Application: read the temperature.
  const uint8_t pointer[1] = {0x00};
  env.i2cWrite(0x48, pointer, 1);
  uint8_t reading[2] = {0};
  env.i2cRead(0x48, reading, 2);
  values->t1 = static_cast<uint16_t>((reading[0] << 8) | reading[1]);

  // Application: AT+S, answered one tick later by the modem.
  const uint64_t before = env.nowMicros();
  env.serialWrite(reinterpret_cast<const uint8_t*>("AT+S;"), 5);
  uint8_t reply[2] = {0};
  env.serialRead(reply, 2, 10000);
  values->reply[0] = static_cast<char>(reply[0]);
  values->reply[1] = static_cast<char>(reply[1]);
  values->elapsed = env.nowMicros() - before;

  env.dump(&temp);
  env.dump(&modem);
  env.formatTrace(out, cap);
}

}  // namespace

int main() {
  printf("NATIVE start\n");
  temp.attach(&env);
  modem.attach(&env);
  env.bindI2c(0x48, &temp);
  env.bindSerial(&modem);
  env.bindChannel(&temp);
  env.addTicking(&temp);
  env.addTicking(&modem);

  static char run1[1600];
  static char run2[1600];
  static char run3[1600];
  Values v1;
  runOnce(run1, sizeof(run1), &v1);
  printf("values t1=%u reply=%s elapsed=%llu\n", v1.t1, v1.reply,
         static_cast<unsigned long long>(v1.elapsed));
  fputs(run1, stdout);
  printf("stats events=%zu dropped=%u\n", env.eventCount(), env.dropped());

  Values v2;
  Values v3;
  runOnce(run2, sizeof(run2), &v2);
  runOnce(run3, sizeof(run3), &v3);
  printf("run2_same=%d run3_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0,
         strcmp(run1, run3) == 0 ? 1 : 0);

  printf("NATIVE done\n");
  return 0;
}
