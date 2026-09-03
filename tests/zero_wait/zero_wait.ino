// Probes re-entry and progress conditions when a 0 us wait is the only
// opportunity to run external processing during a busy-wait loop.
// The guard and injection policy are experiment-local candidates.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostClock.h>

using namespace HostArduino;

static const uint8_t kReadyPin = 5;
static uint64_t virtualNowUs = 0;
static uint32_t zeroWaits = 0;
static uint32_t timedWaits = 0;
static uint32_t depth = 0;
static uint32_t maxDepth = 0;
static uint32_t reentries = 0;
static bool probeDone = false;
static uint32_t injectAt = 0;  // zero-wait count that releases kReadyPin; 0 = never

static uint64_t onNow(void*) { return virtualNowUs; }

static void onWait(uint32_t us, void*) {
  ++depth;
  if (depth > maxDepth) maxDepth = depth;
  if (depth > 1) {
    // Guard candidate: external processing never runs re-entrantly.
    ++reentries;
    --depth;
    return;
  }
  if (us == 0) {
    ++zeroWaits;
    if (!probeDone && zeroWaits == 3) {
      // Probe: does a yield() issued by external processing re-enter here?
      probeDone = true;
      yield();
    }
    if (injectAt != 0 && zeroWaits >= injectAt) setPinValue(kReadyPin, HIGH);
  } else {
    ++timedWaits;
    virtualNowUs += us;
  }
  --depth;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start zero_wait");
  setClockHooks(&onNow, &onWait);

  // Case 1: busy-wait on yield(); the hook releases the pin at the 5th
  // zero wait, so progress comes only from the injection.
  pinMode(kReadyPin, INPUT);
  setPinValue(kReadyPin, LOW);
  injectAt = 5;
  uint32_t spins = 0;
  while (digitalRead(kReadyPin) == LOW && spins < 100000) {
    ++spins;
    yield();
  }
  Serial.printf("case1_spins=%u zero_waits=%u max_depth=%u reentries=%u now=%llu\n",
                spins, zeroWaits, maxDepth, reentries,
                static_cast<unsigned long long>(virtualNowUs));

  // Case 2: nothing ever injects; only the spin cap ends the loop and the
  // virtual clock never moves. This is the infinite-loop condition.
  setPinValue(kReadyPin, LOW);
  injectAt = 0;
  const uint32_t zeroBefore = zeroWaits;
  spins = 0;
  while (digitalRead(kReadyPin) == LOW && spins < 50) {
    ++spins;
    yield();
  }
  Serial.printf("case2_spins=%u zero_wait_delta=%u now=%llu\n", spins,
                zeroWaits - zeroBefore,
                static_cast<unsigned long long>(virtualNowUs));

  // Case 3: a delay(0) busy-wait never reaches the wait hook at all, so a
  // sketch spinning on delay(0) offers no external-processing opportunity.
  const uint32_t zeroBefore3 = zeroWaits;
  const uint32_t timedBefore3 = timedWaits;
  for (uint32_t i = 0; i < 5; ++i) delay(0);
  Serial.printf("case3_zero_delta=%u timed_delta=%u\n", zeroWaits - zeroBefore3,
                timedWaits - timedBefore3);

  clearClockHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }
