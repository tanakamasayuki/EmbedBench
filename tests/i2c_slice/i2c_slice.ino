// WP-C1: the I2C vertical slice. An unmodified Arduino app drives a
// register-map sensor over Wire; a candidate bench core owns the Wire and
// clock hooks, records every external interaction as one ordered event
// list with virtual timestamps, lets the director inject a temperature
// change at a fixed tick, and dumps the device as evidence at the end.
// The whole scenario runs three times and must produce byte-identical
// traces. All structures are experiment-local candidates.
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

using namespace HostArduino;

// --- Register-map temperature sensor model ------------------------------
struct TempSensor {
  uint8_t config = 0;
  uint16_t tempRaw = 0;
  uint8_t pointer = 0;
  uint32_t injectedMutations = 0;

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
      ++injectedMutations;
    }
  }
};

static TempSensor sensor;

// --- Candidate bench core ------------------------------------------------
static const uint32_t kTickUs = 1000;
static char trace[640];
static size_t traceLen = 0;
static uint32_t seq = 0;
static uint64_t virtualNowUs = 0;
static uint64_t nextTickUs = kTickUs;
static uint32_t tickCount = 0;
static bool inTick = false;
static bool injected = false;
static uint32_t injectionEvents = 0;

static void record(const char* origin, const char* detail) {
  ++seq;
  traceLen += snprintf(trace + traceLen, sizeof(trace) - traceLen,
                       "%02u %06llu %s %s %s\n", seq,
                       static_cast<unsigned long long>(virtualNowUs),
                       inTick ? "tick" : "main", origin, detail);
}

static void hexOf(const uint8_t* data, size_t len, char* out) {
  size_t pos = 0;
  for (size_t i = 0; i < len && i < 4; ++i) {
    pos += snprintf(out + pos, 3, "%02X", data[i]);
  }
  out[pos] = '\0';
}

static uint8_t onWireWrite(uint8_t address, const uint8_t* data, size_t len,
                           bool, void*) {
  char hex[12];
  char detail[48];
  hexOf(data, len, hex);
  snprintf(detail, sizeof(detail), "i2c.write addr=%02X data=%s", address, hex);
  record("app", detail);
  return sensor.busWrite(data, len);
}

static size_t onWireRead(uint8_t address, uint8_t* data, size_t len, bool,
                         void*) {
  const size_t count = sensor.busRead(data, len);
  char hex[12];
  char detail[48];
  hexOf(data, count, hex);
  snprintf(detail, sizeof(detail), "i2c.read addr=%02X data=%s", address, hex);
  record("app", detail);
  return count;
}

// Director injection through the bench channel: recorded, then applied.
static void benchChannelWrite(uint8_t channel, const uint8_t* data,
                              size_t len) {
  char hex[12];
  char detail[48];
  hexOf(data, len, hex);
  snprintf(detail, sizeof(detail), "chan.write chan=%u data=%s", channel, hex);
  record("dir", detail);
  ++injectionEvents;
  sensor.onChannelWrite(channel, data, len);
}

static void benchDump() {
  char detail[48];
  snprintf(detail, sizeof(detail), "dump temp=%04X cfg=%02X", sensor.tempRaw,
           sensor.config);
  record("dir", detail);
}

static uint64_t onNow(void*) { return virtualNowUs; }

static void onWait(uint32_t us, void*) {
  const uint64_t target = virtualNowUs + us;
  while (nextTickUs <= target) {
    virtualNowUs = nextTickUs;
    nextTickUs += kTickUs;
    ++tickCount;
    // Director duty: at tick 2 of each run, set the temperature to 300.
    if (tickCount == 2 && !injected) {
      injected = true;
      inTick = true;
      const uint8_t raw300[2] = {0x01, 0x2C};
      benchChannelWrite(0, raw300, 2);
      inTick = false;
    }
  }
  if (target > virtualNowUs) virtualNowUs = target;
}

// --- Application, unmodified Arduino code --------------------------------
static uint16_t appReadTemp() {
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(0x48), static_cast<size_t>(2), true);
  const int first = Wire.read();
  const int second = Wire.read();
  return static_cast<uint16_t>((first << 8) | second);
}

static void appRun(uint16_t* before, uint16_t* after) {
  Wire.beginTransmission(0x48);
  Wire.write(0x01);
  Wire.write(0x05);
  Wire.endTransmission();
  *before = appReadTemp();
  delay(3);
  *after = appReadTemp();
}

// --- One full run: preset, execute, dump ---------------------------------
static void runOnce(char* out, size_t cap, uint16_t* before, uint16_t* after,
                    uint32_t* gap) {
  sensor.reset();
  sensor.tempRaw = 250;  // setup preset: direct, unrecorded (route 3)
  trace[0] = '\0';
  traceLen = 0;
  seq = 0;
  virtualNowUs = 0;
  nextTickUs = kTickUs;
  tickCount = 0;
  inTick = false;
  injected = false;
  injectionEvents = 0;

  setClockHooks(&onNow, &onWait);
  appRun(before, after);
  benchDump();
  clearClockHooks();

  snprintf(out, cap, "%s", trace);
  *gap = sensor.injectedMutations - injectionEvents;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start i2c_slice");

  Wire.begin(21, 22, 400000);
  Wire.setWriteHook(&onWireWrite);
  Wire.setReadHook(&onWireRead);

  static char run1[640];
  static char run2[640];
  static char run3[640];
  uint16_t before = 0;
  uint16_t after = 0;
  uint32_t gap = 0;

  runOnce(run1, sizeof(run1), &before, &after, &gap);
  Serial.printf("before=%u after=%u gap=%u\n", before, after, gap);
  Serial.print(run1);

  uint16_t b2, a2, b3, a3;
  uint32_t g2, g3;
  runOnce(run2, sizeof(run2), &b2, &a2, &g2);
  runOnce(run3, sizeof(run3), &b3, &a3, &g3);
  Serial.printf("run2_same=%d run3_same=%d\n",
                strcmp(run1, run2) == 0 ? 1 : 0,
                strcmp(run1, run3) == 0 ? 1 : 0);

  Wire.clearHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }
