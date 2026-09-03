// Tests a local candidate for splitting arbitrary waits at fixed tick boundaries.
// The algorithm lives in this experiment, not in the public library.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostClock.h>

using namespace HostArduino;

static const uint32_t tickUs = 1000;
static uint64_t virtualNowUs = 0;
static uint64_t nextTickUs = tickUs;
static uint32_t directorCalls = 0;
static uint32_t zeroWaitCalls = 0;

static uint64_t onNow(void*) { return virtualNowUs; }

static void onWait(uint32_t us, void*) {
  if (us == 0) {
    ++zeroWaitCalls;
    return;
  }

  const uint64_t target = virtualNowUs + us;
  while (nextTickUs <= target) {
    virtualNowUs = nextTickUs;
    ++directorCalls;
    nextTickUs += tickUs;
  }
  virtualNowUs = target;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start tick_split");
  setClockHooks(&onNow, &onWait);

  delayMicroseconds(2500);
  Serial.printf("step1_now=%llu ticks=%u next=%llu\n",
                static_cast<unsigned long long>(virtualNowUs), directorCalls,
                static_cast<unsigned long long>(nextTickUs));

  delayMicroseconds(499);
  Serial.printf("step2_now=%llu ticks=%u next=%llu\n",
                static_cast<unsigned long long>(virtualNowUs), directorCalls,
                static_cast<unsigned long long>(nextTickUs));

  delayMicroseconds(1);
  Serial.printf("step3_now=%llu ticks=%u next=%llu\n",
                static_cast<unsigned long long>(virtualNowUs), directorCalls,
                static_cast<unsigned long long>(nextTickUs));

  yield();
  Serial.printf("yield_ticks=%u zero_waits=%u now=%llu\n", directorCalls,
                zeroWaitCalls, static_cast<unsigned long long>(virtualNowUs));

  delay(3);
  Serial.printf("delay_now=%llu ticks=%u next=%llu\n",
                static_cast<unsigned long long>(virtualNowUs), directorCalls,
                static_cast<unsigned long long>(nextTickUs));

  clearClockHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }

