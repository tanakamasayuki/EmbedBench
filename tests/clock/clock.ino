// Measures how each Arduino waiting API reaches the host clock port.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostClock.h>

using namespace HostArduino;

static uint64_t virtualNowUs = 0;
static uint32_t waitCalls = 0;
static uint32_t zeroWaitCalls = 0;
static uint32_t oneMsWaitCalls = 0;
static uint32_t otherWaitCalls = 0;

static uint64_t onNow(void*) { return virtualNowUs; }

static void onWait(uint32_t us, void*) {
  virtualNowUs += us;
  ++waitCalls;
  if (us == 0) ++zeroWaitCalls;
  else if (us == 1000) ++oneMsWaitCalls;
  else ++otherWaitCalls;
}

static void resetWaitCounters() {
  waitCalls = 0;
  zeroWaitCalls = 0;
  oneMsWaitCalls = 0;
  otherWaitCalls = 0;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start clock");
  setClockHooks(&onNow, &onWait);

  resetWaitCounters();
  const uint64_t delayBefore = virtualNowUs;
  delay(3);
  Serial.printf("delay_us=%llu calls=%u zero=%u one_ms=%u other=%u\n",
                static_cast<unsigned long long>(virtualNowUs - delayBefore), waitCalls,
                zeroWaitCalls, oneMsWaitCalls, otherWaitCalls);

  resetWaitCounters();
  const uint64_t microBefore = virtualNowUs;
  delayMicroseconds(2500);
  Serial.printf("delay_micro_us=%llu calls=%u zero=%u one_ms=%u other=%u\n",
                static_cast<unsigned long long>(virtualNowUs - microBefore), waitCalls,
                zeroWaitCalls, oneMsWaitCalls, otherWaitCalls);

  resetWaitCounters();
  delay(0);
  Serial.printf("delay_zero_calls=%u\n", waitCalls);

  resetWaitCounters();
  delayMicroseconds(0);
  yield();
  Serial.printf("zero_wait_calls=%u total_calls=%u\n", zeroWaitCalls, waitCalls);

  resetWaitCounters();
  Serial1.begin(9600);
  Serial1.setTimeout(4);
  uint8_t byte = 0;
  const uint64_t streamBefore = virtualNowUs;
  const size_t received = Serial1.readBytes(&byte, 1);
  Serial.printf("stream_timeout_us=%llu calls=%u received=%u\n",
                static_cast<unsigned long long>(virtualNowUs - streamBefore), waitCalls,
                static_cast<unsigned>(received));

  clearClockHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }

