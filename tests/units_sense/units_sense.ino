// The self-driven units: devices that keep going whether or not the
// application is asking. An IMU samples into a FIFO on its own clock and
// loses data when the sketch is late; an RTC keeps a wall clock in
// seconds and raises an alarm at a time, not after an interval.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <Wire.h>
#include <embedbench_draft.h>
#include <string.h>

#include <unit_imu_model.h>
#include <unit_rtc_model.h>

static UnitImuModel imu;
static UnitRtcModel rtc;

static const uint8_t kPinImuIrq = 33;
static const uint8_t kPinRtcInt = 34;
static const uint8_t kAddrImu = 0x68;
static const uint8_t kAddrRtc = 0x51;

// [adapter begin]
class SensePort : public ebdev::HostPort {
 public:
  explicit SensePort(uint8_t linePin) : linePin_(linePin) {}
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    ebd::pinInject(ebd::Origin::kDev, linePin_, level);
  }
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool requestWake(uint64_t whenUs) override { return ebd::requestWake(whenUs); }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
    return true;
  }

 private:
  uint8_t linePin_;
};

static SensePort imuPort(kPinImuIrq);
static SensePort rtcPort(kPinRtcInt);

static uint8_t imuWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return imu.i2cWrite(data, len, xfer);
}
static size_t imuRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return imu.i2cRead(data, len, xfer);
}
static uint8_t rtcWrite(const uint8_t* data, size_t len, bool stop,
                        bool continued, void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return rtc.i2cWrite(data, len, xfer);
}
static size_t rtcRead(uint8_t* data, size_t len, bool stop, bool continued,
                      void*) {
  const ebdev::I2cTransfer xfer = {stop, continued};
  return rtc.i2cRead(data, len, xfer);
}
static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  if (channel != 0) return false;
  return imu.channelWrite(UnitImuModel::kChannelValue, data, len);
}
static void advanceSense(uint64_t nowUs, void*) {
  imu.advanceTo(nowUs);
  rtc.advanceTo(nowUs);
}
// [adapter end]

// --- Application ------------------------------------------------------------
static uint8_t readStatus(uint8_t addr, uint8_t reg, uint8_t* out, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(addr), n, true);
  size_t got = 0;
  while (Wire.available() && got < n) out[got++] = Wire.read();
  return static_cast<uint8_t>(got);
}

static uint8_t firstCount = 0;
static uint8_t firstFlags = 0;
static size_t firstBytes = 0;
static uint16_t firstSample = 0;
static int irqAfterDrain = -1;
static uint8_t lateCount = 0;
static uint8_t lateFlags = 0;
static uint32_t rtcStart = 0;
static uint32_t rtcAtAlarm = 0;
static int rtcIntLevel = -1;
static uint8_t rtcFired = 0;
static uint8_t alarmPastStatus = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_sense");
  Wire.begin(21, 22, 400000);
  pinMode(kPinImuIrq, INPUT);
  pinMode(kPinRtcInt, INPUT);

  imu.attach(&imuPort);
  rtc.attach(&rtcPort);
  const ebd::WireDeviceOps imuOps = {&imuWrite, &imuRead, nullptr};
  const ebd::WireDeviceOps rtcOps = {&rtcWrite, &rtcRead, nullptr};
  ebd::bindWireDevice(kAddrImu, imuOps);
  ebd::bindWireDevice(kAddrRtc, rtcOps);
  ebd::setChannelHandler(&routeChannel);
  ebd::bindTickDevice(&advanceSense);
  imu.reset();
  rtc.reset();

  ebd::runBegin(10000);  // a 10 ms tick: the 2.5 ms sampling never lands on it

  // The world presents a value, then the sketch starts the sensor.
  const uint8_t value[2] = {0x12, 0x34};
  ebd::chanWrite(ebd::Origin::kDir, 0, value, 2);
  Wire.beginTransmission(kAddrImu);
  Wire.write(UnitImuModel::kRegControl);
  Wire.write(1);
  Wire.endTransmission();

  // Come back once the watermark is due: 8 samples at 2.5 ms is 20 ms.
  delay(21);
  uint8_t status[2] = {0, 0};
  readStatus(kAddrImu, UnitImuModel::kRegStatus, status, 2);
  firstCount = status[0];
  firstFlags = status[1];
  // Ask for more than is there: what comes back is the FIFO's business.
  uint8_t burst[40] = {0};
  Wire.beginTransmission(kAddrImu);
  Wire.write(UnitImuModel::kRegFifo);
  Wire.endTransmission();
  Wire.requestFrom(static_cast<uint16_t>(kAddrImu), static_cast<size_t>(40),
                   true);
  while (Wire.available() && firstBytes < sizeof(burst)) {
    burst[firstBytes++] = Wire.read();
  }
  firstSample = static_cast<uint16_t>((burst[0] << 8) | burst[1]);
  irqAfterDrain = digitalRead(kPinImuIrq);

  // Now be late: 16 slots at 2.5 ms is 40 ms, so 60 ms away loses data.
  delay(60);
  readStatus(kAddrImu, UnitImuModel::kRegStatus, status, 2);
  lateCount = status[0];
  lateFlags = status[1];
  Wire.beginTransmission(kAddrImu);
  Wire.write(UnitImuModel::kRegControl);
  Wire.write(0);
  Wire.endTransmission();

  // The RTC keeps a clock in seconds. Set it, refuse an alarm that has
  // already gone, then set one two seconds out and wait for the line.
  const uint8_t setTime[5] = {UnitRtcModel::kRegTime, 0x00, 0x00, 0x03, 0xE8};
  Wire.beginTransmission(kAddrRtc);
  Wire.write(setTime, sizeof(setTime));
  Wire.endTransmission();
  uint8_t stamp[4] = {0, 0, 0, 0};
  readStatus(kAddrRtc, UnitRtcModel::kRegTime, stamp, 4);
  rtcStart = (static_cast<uint32_t>(stamp[2]) << 8) | stamp[3];

  const uint8_t past[5] = {UnitRtcModel::kRegAlarm, 0x00, 0x00, 0x03, 0xE7};
  Wire.beginTransmission(kAddrRtc);
  Wire.write(past, sizeof(past));
  alarmPastStatus = Wire.endTransmission();

  const uint8_t soon[5] = {UnitRtcModel::kRegAlarm, 0x00, 0x00, 0x03, 0xEA};
  Wire.beginTransmission(kAddrRtc);
  Wire.write(soon, sizeof(soon));
  Wire.endTransmission();
  delay(2100);
  rtcIntLevel = digitalRead(kPinRtcInt);
  readStatus(kAddrRtc, UnitRtcModel::kRegTime, stamp, 4);
  rtcAtAlarm = (static_cast<uint32_t>(stamp[2]) << 8) | stamp[3];
  uint8_t fired[1] = {0};
  readStatus(kAddrRtc, UnitRtcModel::kRegStatus, fired, 1);
  rtcFired = fired[0];

  char text[64];
  imu.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  rtc.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();

  static char trace[6144];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values count=%u flags=%u bytes=%u sample=%04X irq=%d\n",
                firstCount, firstFlags, static_cast<unsigned>(firstBytes),
                firstSample, irqAfterDrain);
  Serial.printf("values late_count=%u late_flags=%u\n", lateCount, lateFlags);
  Serial.printf("values rtc_start=%u rtc_end=%u int=%d fired=%u past=%u\n",
                rtcStart, rtcAtAlarm, rtcIntLevel, rtcFired, alarmPastStatus);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u folded=%u diag=%u\n", s.events,
                s.dropped, s.folded, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
