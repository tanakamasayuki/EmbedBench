// Records the lifecycle order around setup() and two loop() iterations.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostClock.h>
#include <HostLifecycle.h>

using namespace HostArduino;

static char sequence[32] = {0};
static size_t sequenceLength = 0;
static uint32_t appLoops = 0;
static bool finished = false;

static void note(char value) {
  if (sequenceLength + 1 < sizeof(sequence)) {
    sequence[sequenceLength++] = value;
    sequence[sequenceLength] = '\0';
  }
}

static void onPhase(LifecyclePhase phase, void*) {
  switch (phase) {
    case kPreSetup: note('A'); break;
    case kPostSetup: note('B'); break;
    case kPreLoop: note('C'); break;
    case kPostLoop: note('D'); break;
  }
}

struct LifecycleInstaller {
  LifecycleInstaller() { setLifecycleHook(&onPhase); }
};

static LifecycleInstaller installer;

void setup() {
  note('S');
  Serial.begin(115200);
  Serial.println("TEST start lifecycle");
}

void loop() {
  ++appLoops;
  note('L');

  if (appLoops == 2 && !finished) {
    finished = true;
    Serial.print("sequence=");
    Serial.println(sequence);
    Serial.print("sequence_length=");
    Serial.println(static_cast<unsigned long>(sequenceLength));
    Serial.print("completed_loops_before_return=");
    Serial.println(static_cast<unsigned long>(HostArduino::loopCount()));
    Serial.println("TEST done");
  }

  if (finished) clockRealWaitMicros(10000);
}
