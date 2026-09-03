// Integration measurement of the draft core (src/embedbench_draft.h): an
// unmodified Arduino app that spans GPIO, an interrupt, I2C, UART, and
// virtual time in one run. The director injects the DRDY edge during a
// busy-wait, changes the temperature at tick 2, and the UART device
// replies through the RX sink. Runs three times; traces must match.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostInterrupt.h>
#include <HostUart.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

// --- Register-map temperature sensor model (as X18) -----------------------
struct TempSensor {
  uint8_t config = 0;
  uint16_t tempRaw = 0;
  uint8_t pointer = 0;

  void reset() { *this = TempSensor(); }

  uint8_t busWrite(const uint8_t* data, size_t len) {
    if (len == 1) {
      pointer = data[0];
    } else if (len == 2 && data[0] == 0x01) {
      config = data[1];
    }
    return 0;
  }
  size_t busRead(uint8_t* data, size_t len) {
    if (pointer == 0x00 && len >= 2) {
      data[0] = static_cast<uint8_t>(tempRaw >> 8);
      data[1] = static_cast<uint8_t>(tempRaw & 0xFF);
      return 2;
    }
    return 0;
  }
  void onChannelWrite(uint8_t channel, const uint8_t* data, size_t len) {
    if (channel == 0 && len == 2) {
      tempRaw = static_cast<uint16_t>((data[0] << 8) | data[1]);
    }
  }
};

static TempSensor sensor;
static volatile bool dataReady = false;

// --- Bindings into the draft core -----------------------------------------
static uint8_t devWrite(const uint8_t* data, size_t len, void*) {
  return sensor.busWrite(data, len);
}
static size_t devRead(uint8_t* data, size_t len, void*) {
  return sensor.busRead(data, len);
}
static void devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  sensor.onChannelWrite(channel, data, len);
}
static void devUartTx(const uint8_t* data, size_t len, void*) {
  if (len == 2 && data[0] == 'A' && data[1] == 'T') {
    ebd::uartInject(ebd::Origin::kDev, "OK");
  }
}
static void onTick(uint32_t tick, void*) {
  if (tick == 2) {
    const uint8_t raw300[2] = {0x01, 0x2C};
    ebd::chanWrite(ebd::Origin::kDir, 0, raw300, 2);
  }
}
static void onZeroWait(uint32_t count, void*) {
  if (count == 3) ebd::pinInject(ebd::Origin::kDir, 27, HIGH);
}

// --- Application: unmodified Arduino code ----------------------------------
static void onDrdy() {
  digitalWrite(5, HIGH);
  dataReady = true;
}

static uint16_t readTemp() {
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(0x48), static_cast<size_t>(2), true);
  const int first = Wire.read();
  const int second = Wire.read();
  return static_cast<uint16_t>((first << 8) | second);
}

static uint16_t appT1 = 0;
static uint16_t appT2 = 0;
static uint32_t appSpins = 0;

static void appScenario() {
  Wire.beginTransmission(0x48);
  Wire.write(0x01);
  Wire.write(0x05);
  Wire.endTransmission();

  attachInterrupt(27, &onDrdy, RISING);
  uint32_t spins = 0;
  while (!dataReady && spins < 1000) {
    ++spins;
    yield();
  }
  appSpins = spins;

  appT1 = readTemp();

  Serial1.print("AT");
  uint8_t reply[2] = {0};
  Serial1.readBytes(reply, sizeof(reply));

  delay(2);

  appT2 = readTemp();
}

// --- One run ---------------------------------------------------------------
static void runOnce(char* out, size_t cap) {
  sensor.reset();
  sensor.tempRaw = 250;  // setup preset: direct, unrecorded (route 3)
  dataReady = false;
  HostArduino::setPinValue(5, LOW);
  HostArduino::setPinValue(27, LOW);
  HostArduino::resetInterrupts();
  uint8_t drain[16];
  while (Serial1.readTx(drain, sizeof(drain)) > 0) {
  }

  ebd::runBegin(1000);
  appScenario();
  ebd::dumpf("temp=%04X cfg=%02X", sensor.tempRaw, sensor.config);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1600];
static char run2[1600];
static char run3[1600];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start core_draft");

  Wire.begin(21, 22, 400000);
  Serial1.begin(9600);
  Serial1.setTimeout(10);
  pinMode(5, OUTPUT);
  pinMode(27, INPUT);

  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x48, ops);
  ebd::bindUartDevice(&devUartTx);
  ebd::setChannelHandler(&devChannel);
  ebd::setTickHandler(&onTick);
  ebd::setZeroWaitHandler(&onZeroWait);

  runOnce(run1, sizeof(run1));
  Serial.printf("values t1=%u t2=%u spins=%u\n", appT1, appT2, appSpins);
  Serial.print(run1);
  const ebd::Stats s = ebd::stats();
  Serial.printf(
      "stats events=%u dropped=%u zero_waits=%u zero_in_dir=%u late_ticks=%u "
      "ticks=%u diag=%u\n",
      s.events, s.dropped, s.zeroWaits, s.zeroInDirector, s.lateTicks, s.ticks,
      s.diagCount);
  Serial.printf("metrics event_bytes=%u resp_lines=%u\n",
                static_cast<unsigned>(ebd::eventBytes()),
                static_cast<unsigned>(ebd::respLineCount()));

  runOnce(run2, sizeof(run2));
  runOnce(run3, sizeof(run3));
  Serial.printf("run2_same=%d run3_same=%d\n",
                strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);

  Serial.println("TEST done");
}

void loop() { delay(10); }
