// The same pure-C++ device models (temp_model / modem_model), unchanged,
// hosted on the Arduino host core through a thin adapter onto the draft
// core. The environment side is a platform implementation example; the
// device interface (src/embedbench_device.h) is the surface being fixed.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostInterrupt.h>
#include <HostUart.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <stdio.h>
#include <string.h>

#include <modem_model.h>
#include <temp_model.h>

static TempSensorModel tempModel;
static AtModemModel modemModel;

// --- Platform adapter: pure device IF -> draft core sinks -----------------
// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t line, uint8_t level) override {
    // Logical line 0 of the temp sensor is wired to pin 27 here.
    const uint8_t pin = line == TempSensorModel::kLineDataReady ? 27 : 0xFF;
    if (pin != 0xFF) ebd::pinInject(ebd::Origin::kDev, pin, level);
  }
  void serialOut(const uint8_t* data, size_t len) override {
    char text[16];
    snprintf(text, sizeof(text), "%.*s", static_cast<int>(len),
             reinterpret_cast<const char*>(data));
    ebd::uartInject(ebd::Origin::kDev, text);
  }
};

static DraftPort draftPort;

static uint8_t devWrite(const uint8_t* data, size_t len, void*) {
  return tempModel.i2cWrite(data, len);
}
static size_t devRead(uint8_t* data, size_t len, void*) {
  return tempModel.i2cRead(data, len);
}
static void devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  tempModel.channelWrite(channel, data, len);
}
static void devUartTx(const uint8_t* data, size_t len, void*) {
  modemModel.serialIn(data, len);
}
static void onTick(uint32_t, void*) {
  tempModel.advanceTo(ebd::nowUs());
  modemModel.advanceTo(ebd::nowUs());
}
// [adapter end]

static void onZeroWait(uint32_t count, void*) {
  if (count == 3) {
    const uint8_t raw300[2] = {0x01, 0x2C};
    ebd::chanWrite(ebd::Origin::kDir, 0, raw300, 2);
  }
}

// --- Application: unmodified Arduino code ----------------------------------
static volatile bool dataReady = false;

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
static uint32_t appSpins = 0;
static char appReply[3] = {0};
static uint64_t appElapsed = 0;

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

  const uint64_t before = ebd::nowUs();
  Serial1.print("AT+S");
  uint8_t reply[2] = {0};
  Serial1.readBytes(reply, sizeof(reply));
  appReply[0] = static_cast<char>(reply[0]);
  appReply[1] = static_cast<char>(reply[1]);
  appElapsed = ebd::nowUs() - before;
}

// --- One run ---------------------------------------------------------------
static void runOnce(char* out, size_t cap) {
  HostArduino::resetInterrupts();
  tempModel.reset();
  modemModel.reset();
  // Setup preset through the model's own channel (direct, unrecorded);
  // afterwards settle the lines the preset may have raised.
  const uint8_t raw250[2] = {0x00, 0xFA};
  tempModel.channelWrite(0, raw250, 2);
  HostArduino::setPinValue(27, LOW);
  HostArduino::setPinValue(5, LOW);
  dataReady = false;
  uint8_t drain[16];
  while (Serial1.readTx(drain, sizeof(drain)) > 0) {
  }

  ebd::runBegin(1000);
  appScenario();
  char text[40];
  tempModel.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  modemModel.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1600];
static char run2[1600];
static char run3[1600];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start device_if");

  Wire.begin(21, 22, 400000);
  Serial1.begin(9600);
  Serial1.setTimeout(10);
  pinMode(5, OUTPUT);
  pinMode(27, INPUT);

  tempModel.attach(&draftPort);
  modemModel.attach(&draftPort);
  const ebd::WireDeviceOps ops = {&devWrite, &devRead, nullptr};
  ebd::bindWireDevice(0x48, ops);
  ebd::bindUartDevice(&devUartTx);
  ebd::setChannelHandler(&devChannel);
  ebd::setTickHandler(&onTick);
  ebd::setZeroWaitHandler(&onZeroWait);

  runOnce(run1, sizeof(run1));
  Serial.printf("values t1=%u spins=%u reply=%s elapsed=%llu\n", appT1,
                appSpins, appReply,
                static_cast<unsigned long long>(appElapsed));
  Serial.print(run1);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u resp_lines=%u diag=%u\n",
                s.events, s.dropped,
                static_cast<unsigned>(ebd::respLineCount()), s.diagCount);

  runOnce(run2, sizeof(run2));
  runOnce(run3, sizeof(run3));
  Serial.printf("run2_same=%d run3_same=%d\n",
                strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);

  Serial.println("TEST done");
}

void loop() { delay(10); }
