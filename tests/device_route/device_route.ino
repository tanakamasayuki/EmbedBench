// WP-A2: compares three routing policies for driving the same register-map
// sensor model — everything direct, everything through a common channel
// interface, and setup/inspection direct with runtime mutations through the
// channel. All structures are experiment-local candidates.
//
// The scenario is identical for every route:
//   setup:   temperature raw register preset to 250
//   run:     app writes config=5 over I2C, the director changes the
//            temperature to 300, the app reads the temperature over I2C
//   inspect: read the config register back
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <string.h>

// --- Bench candidate: owns the Wire hooks, records events as tag chars ---
// w = i2c write observed, r = i2c read observed, s = setup channel write,
// j = runtime injection channel write, i = inspection channel read.
static char trace[32];
static size_t traceLen = 0;
static uint32_t channelCallbacks = 0;  // device channel handler invocations
static uint32_t conversions = 0;       // encode/decode helper calls

static void record(char tag) {
  if (traceLen < sizeof(trace) - 1) {
    trace[traceLen++] = tag;
    trace[traceLen] = '\0';
  }
}

// Byte conversion helpers; every call is counted as one type conversion.
static void encodeU16(uint16_t value, uint8_t* out) {
  ++conversions;
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value & 0xFF);
}
static uint16_t decodeU16(const uint8_t* in) {
  ++conversions;
  return static_cast<uint16_t>((in[0] << 8) | in[1]);
}
static void encodeU8(uint8_t value, uint8_t* out) {
  ++conversions;
  out[0] = value;
}
static uint8_t decodeU8(const uint8_t* in) {
  ++conversions;
  return in[0];
}

// --- Register-map temperature sensor model ------------------------------
// Registers: 0x00 temperature raw (2 bytes, read-only over the bus),
// 0x01 config (1 byte, read/write over the bus).
// Channels: 0 = temperature raw (2 bytes), 1 = config (1 byte).
struct TempSensor {
  uint8_t config = 0;
  uint16_t tempRaw = 0;
  uint8_t pointer = 0;
  bool running = false;
  uint32_t runMutations = 0;  // externally injected changes inside the run

  void reset() { *this = TempSensor(); }

  // [device-typed-api begin]
  void setTempRawDirect(uint16_t raw) {
    tempRaw = raw;
    if (running) ++runMutations;
  }
  uint16_t tempRawDirect() const { return tempRaw; }
  uint8_t configDirect() const { return config; }
  // [device-typed-api end]

  // [device-channel-adapter begin]
  void onChannelWrite(uint8_t channel, const uint8_t* data, size_t len) {
    if (channel == 0 && len == 2) {
      tempRaw = decodeU16(data);
      if (running) ++runMutations;
    } else if (channel == 1 && len == 1) {
      config = decodeU8(data);
      if (running) ++runMutations;
    }
  }
  size_t onChannelRead(uint8_t channel, uint8_t* out, size_t cap) {
    if (channel == 0 && cap >= 2) {
      encodeU16(tempRaw, out);
      return 2;
    }
    if (channel == 1 && cap >= 1) {
      encodeU8(config, out);
      return 1;
    }
    return 0;
  }
  // [device-channel-adapter end]

  // Bus protocol, always reached through the bench-owned Wire hooks.
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
    if (pointer == 0x01 && len >= 1) {
      data[0] = config;
      return 1;
    }
    return 0;
  }
};

static TempSensor sensor;

// Bench channel entry points: record first, then forward to the device.
static void benchChannelWrite(uint8_t channel, const uint8_t* data,
                              size_t len) {
  record(sensor.running ? 'j' : 's');
  ++channelCallbacks;
  sensor.onChannelWrite(channel, data, len);
}
static size_t benchChannelRead(uint8_t channel, uint8_t* out, size_t cap) {
  record('i');
  ++channelCallbacks;
  return sensor.onChannelRead(channel, out, cap);
}

static uint8_t onWireWrite(uint8_t, const uint8_t* data, size_t len, bool,
                           void*) {
  record('w');
  return sensor.busWrite(data, len);
}
static size_t onWireRead(uint8_t, uint8_t* data, size_t len, bool, void*) {
  record('r');
  return sensor.busRead(data, len);
}

// --- Application code, identical for every route -------------------------
static void appWriteConfig() {
  Wire.beginTransmission(0x48);
  Wire.write(0x01);
  Wire.write(0x05);
  Wire.endTransmission();
}

static uint16_t appReadTemp() {
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(0x48), static_cast<size_t>(2), true);
  const int first = Wire.read();
  const int second = Wire.read();
  return static_cast<uint16_t>((first << 8) | second);
}

static void beginRoute() {
  sensor.reset();
  trace[0] = '\0';
  traceLen = 0;
  channelCallbacks = 0;
  conversions = 0;
}

static void report(const char* name, uint16_t temp, uint8_t cfg) {
  uint32_t injectionEvents = 0;
  for (size_t i = 0; i < traceLen; ++i) {
    if (trace[i] == 'j') ++injectionEvents;
  }
  const uint32_t gap = sensor.runMutations - injectionEvents;
  Serial.printf(
      "%s temp=%u cfg=%u events=%u trace=<%s> channel_cb=%u conv=%u gap=%u\n",
      name, temp, cfg, static_cast<unsigned>(traceLen), trace,
      channelCallbacks, conversions, gap);
}

// --- Route 1: everything through the device's typed API ------------------
// [route1-caller begin]
static void route1() {
  sensor.setTempRawDirect(250);
  sensor.running = true;
  appWriteConfig();
  sensor.setTempRawDirect(300);  // runtime injection bypasses the bench
  const uint16_t temp = appReadTemp();
  sensor.running = false;
  const uint8_t cfg = sensor.configDirect();
  report("route1_direct", temp, cfg);
}
// [route1-caller end]

// --- Route 2: everything through the common channel interface ------------
// [route2-caller begin]
static void route2() {
  uint8_t buf[2];
  encodeU16(250, buf);
  benchChannelWrite(0, buf, 2);
  sensor.running = true;
  appWriteConfig();
  encodeU16(300, buf);
  benchChannelWrite(0, buf, 2);
  const uint16_t temp = appReadTemp();
  sensor.running = false;
  uint8_t out[1];
  benchChannelRead(1, out, 1);
  const uint8_t cfg = decodeU8(out);
  report("route2_common", temp, cfg);
}
// [route2-caller end]

// --- Route 3: setup/inspection direct, runtime mutation via channel ------
// [route3-caller begin]
static void route3() {
  sensor.setTempRawDirect(250);
  sensor.running = true;
  appWriteConfig();
  uint8_t buf[2];
  encodeU16(300, buf);
  benchChannelWrite(0, buf, 2);
  const uint16_t temp = appReadTemp();
  sensor.running = false;
  const uint8_t cfg = sensor.configDirect();
  report("route3_split", temp, cfg);
}
// [route3-caller end]

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start device_route");

  Wire.begin(21, 22, 400000);
  Wire.setWriteHook(&onWireWrite);
  Wire.setReadHook(&onWireRead);

  beginRoute();
  route1();
  beginRoute();
  route2();
  beginRoute();
  route3();

  Wire.clearHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }
