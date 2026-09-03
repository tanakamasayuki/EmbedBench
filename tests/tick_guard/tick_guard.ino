// Compares three experiment-local policies for a wait API being called
// from inside a fixed-tick director callback: allow re-entrant splitting,
// advance time but defer tick firing, or reject the nested wait.
// The core's delay() loops on clockNowMicros() until its deadline, so a
// rejection that freezes the clock can never let delay() return.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostClock.h>
#include <stdio.h>
#include <string.h>

using namespace HostArduino;

enum class Policy : uint8_t { kAllow, kDeferTicks, kReject };

static const uint32_t kTickUs = 1000;
static const uint32_t kRejectCap = 50;

static Policy policy = Policy::kAllow;
static uint64_t virtualNowUs = 0;
static uint64_t nextTickUs = kTickUs;
static uint32_t ticksFired = 0;
static uint32_t directorDepth = 0;
static uint32_t maxDirectorDepth = 0;
static uint32_t pendingTicks = 0;
static uint32_t lateTicks = 0;
static uint32_t rejectedCalls = 0;
static uint64_t rejectedUs = 0;
static uint32_t capHits = 0;
static uint32_t backwardJumps = 0;
static uint32_t zeroInTick = 0;
static void (*nestedApi)() = nullptr;
static char order[96];
static size_t orderLen = 0;

static uint64_t onNow(void*) { return virtualNowUs; }

static void fireTick() {
  ++ticksFired;
  if (orderLen < sizeof(order) - 12) {
    orderLen += snprintf(order + orderLen, sizeof(order) - orderLen, "%u@%llu ",
                         ticksFired,
                         static_cast<unsigned long long>(virtualNowUs));
  }
  ++directorDepth;
  if (directorDepth > maxDirectorDepth) maxDirectorDepth = directorDepth;
  if (ticksFired == 2 && nestedApi) {
    void (*api)() = nestedApi;
    nestedApi = nullptr;
    api();
  }
  --directorDepth;
}

static void onWait(uint32_t us, void*) {
  if (us == 0) {
    if (directorDepth > 0) ++zeroInTick;
    return;
  }
  if (directorDepth > 0) {
    if (policy == Policy::kReject) {
      ++rejectedCalls;
      rejectedUs += us;
      if (rejectedCalls >= kRejectCap) {
        // Escape hatch: without it, the core's delay() loop would spin
        // forever because the frozen clock never reaches its deadline.
        ++capHits;
        virtualNowUs += us;
      }
      return;
    }
    if (policy == Policy::kDeferTicks) {
      // Advance time so the nested caller can finish, but let the outer
      // splitter fire the crossed boundaries after the tick returns.
      const uint64_t target = virtualNowUs + us;
      while (nextTickUs <= target) {
        ++pendingTicks;
        nextTickUs += kTickUs;
      }
      virtualNowUs = target;
      return;
    }
    // kAllow falls through to the normal re-entrant split below.
  }
  const uint64_t target = virtualNowUs + us;
  while (nextTickUs <= target) {
    if (nextTickUs < virtualNowUs) ++backwardJumps;
    virtualNowUs = nextTickUs;
    nextTickUs += kTickUs;
    fireTick();
    while (pendingTicks > 0) {
      --pendingTicks;
      ++lateTicks;
      fireTick();
    }
  }
  if (target > virtualNowUs) virtualNowUs = target;
}

static void nestedDelay() { delay(2); }
static void nestedDelayMicros() { delayMicroseconds(500); }
static void nestedYield() { yield(); }

static void runScenario(const char* name, Policy p, void (*api)()) {
  policy = p;
  virtualNowUs = 0;
  nextTickUs = kTickUs;
  ticksFired = 0;
  directorDepth = 0;
  maxDirectorDepth = 0;
  pendingTicks = 0;
  lateTicks = 0;
  rejectedCalls = 0;
  rejectedUs = 0;
  capHits = 0;
  backwardJumps = 0;
  zeroInTick = 0;
  nestedApi = api;
  order[0] = '\0';
  orderLen = 0;

  setClockHooks(&onNow, &onWait);
  delay(5);
  clearClockHooks();

  Serial.printf(
      "%s ticks=%u depth=%u late=%u rejected=%u rejected_us=%llu cap=%u "
      "backward=%u zero=%u now=%llu order=%s\n",
      name, ticksFired, maxDirectorDepth, lateTicks, rejectedCalls,
      static_cast<unsigned long long>(rejectedUs), capHits, backwardJumps,
      zeroInTick, static_cast<unsigned long long>(virtualNowUs), order);
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start tick_guard");

  runScenario("s1_allow_delay", Policy::kAllow, &nestedDelay);
  runScenario("s2_defer_delay", Policy::kDeferTicks, &nestedDelay);
  runScenario("s3_reject_delay", Policy::kReject, &nestedDelay);
  runScenario("s4_reject_micros", Policy::kReject, &nestedDelayMicros);
  runScenario("s5_reject_yield", Policy::kReject, &nestedYield);

  Serial.println("TEST done");
}

void loop() { delay(10); }
