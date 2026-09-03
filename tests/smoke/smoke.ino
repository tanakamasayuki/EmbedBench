// Minimal end-to-end check of the experiment environment:
// Arduino library resolution -> host core -> lifecycle/clock ports -> pytest.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostClock.h>
#include <HostLifecycle.h>

using namespace HostArduino;

static uint64_t virtualNowUs = 0;
static uint32_t waitCalls = 0;
static uint32_t preSetupCalls = 0;
static uint32_t postSetupCalls = 0;
static uint32_t preLoopCalls = 0;
static uint32_t postLoopCalls = 0;
static uint32_t appLoops = 0;
static uint32_t delayElapsedUs = 0;
static bool finished = false;

static uint64_t onNow(void*) { return virtualNowUs; }

static void onWait(uint32_t us, void*) {
  virtualNowUs += us;
  ++waitCalls;
}

static void onPhase(LifecyclePhase phase, void*) {
  switch (phase) {
    case kPreSetup:  ++preSetupCalls; break;
    case kPostSetup: ++postSetupCalls; break;
    case kPreLoop:   ++preLoopCalls; break;
    case kPostLoop:
      ++postLoopCalls;
      virtualNowUs += 1000;  // one completed loop is one provisional 1 ms tick
      break;
  }
}

// Installation must happen before main() so kPreSetup remains observable.
struct HostPortInstaller {
  HostPortInstaller() {
    setClockHooks(&onNow, &onWait);
    setLifecycleHook(&onPhase);
  }
};

static HostPortInstaller installer;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start smoke");
  Serial.print("library_version=");
  Serial.println(EMBEDBENCH_VERSION);
}

void loop() {
  ++appLoops;

  if (appLoops == 1) {
    const uint32_t before = micros();
    delay(3);
    delayElapsedUs = micros() - before;
    return;
  }

  if (appLoops == 2 && !finished) {
    finished = true;
    Serial.print("clock_overridden=");
    Serial.println(clockOverridden() ? 1 : 0);
    Serial.print("delay_elapsed_us=");
    Serial.println(delayElapsedUs);
    Serial.print("wait_calls=");
    Serial.println(waitCalls);
    Serial.printf("phases=%u,%u,%u,%u\n", preSetupCalls, postSetupCalls,
                  preLoopCalls, postLoopCalls);
    Serial.println("TEST done");
  }

  // The virtual clock never sleeps, so throttle the completed sketch using
  // the explicit real-time port until pytest terminates it.
  if (finished) clockRealWaitMicros(10000);
}
