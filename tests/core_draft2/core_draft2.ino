// Draft core v2: the three pieces the first round left out — analog
// observation and injection, a lifecycle-driven run window that the core
// closes on its own, and event listeners that watch the stream without
// being able to change it.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_internals.h>
#include <HostBus.h>
#include <stdio.h>
#include <string.h>

// --- Listeners: two observers plus one that removes itself -----------------
static uint32_t seenA = 0;
static uint32_t seenB = 0;
static uint32_t seenOnce = 0;
static char lastA[32] = {0};

static void listenerA(const ebhost::Event& e, void*) {
  ++seenA;
  snprintf(lastA, sizeof(lastA), "%s", e.text);
}
static void listenerB(const ebhost::Event&, void*) { ++seenB; }
static void listenerOnce(const ebhost::Event&, void*) {
  ++seenOnce;
  ebhost::removeListener(&listenerOnce);
}
static void listenerExtra(const ebhost::Event&, void*) {}

// --- The run window is armed before main, the only point where the host
// core's lifecycle hook still catches preSetup.
struct WindowArmer {
  WindowArmer() { ebhost::armRunWindow(1000, 2); }
};
static WindowArmer armer;

// --- Application: ordinary Arduino code ------------------------------------
static uint16_t appRaw = 0;
static uint32_t appMv = 0;
static uint32_t loopRuns = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start core_draft2");

  // Listeners: A and B watch, "once" removes itself from its own callback,
  // and a fifth registration is refused by the fixed table.
  ebhost::addListener(&listenerA);
  ebhost::addListener(&listenerB);
  ebhost::addListener(&listenerOnce);
  ebhost::addListener(&listenerExtra);
  const bool fifth = ebhost::addListener(&listenerA, reinterpret_cast<void*>(1));

  // Analog: the director injects held values, the application reads them,
  // and a PWM write is observed.
  ebhost::analogInject(ebhost::Origin::kDir, 8, 1234);
  ebhost::analogInjectMilliVolts(ebhost::Origin::kDir, 8, 3300);
  analogReadResolution(10);
  appRaw = analogRead(8);
  appMv = analogReadMilliVolts(8);
  analogWrite(9, 128);

  Serial.printf("values raw=%u mv=%u fifth=%d capacity=%u\n", appRaw, appMv,
                fifth ? 1 : 0,
                static_cast<unsigned>(ebhost::listenerCapacity()));
}

void loop() {
  ++loopRuns;
  digitalWrite(4, loopRuns % 2 == 0 ? LOW : HIGH);
  if (loopRuns == 3) {
    // The window closed after the second loop: this third iteration must
    // leave no trace behind.
    static char trace[2048];
    ebhost::formatTrace(trace, sizeof(trace));
    Serial.print(trace);
    const ebhost::Stats s = ebhost::stats();
    Serial.printf("stats events=%u dropped=%u closed=%d loops=%u\n", s.events,
                  s.dropped, ebhost::runWindowClosed() ? 1 : 0,
                  ebhost::completedLoops());
    Serial.printf("listeners a=%u b=%u once=%u last_a=%s\n", seenA, seenB,
                  seenOnce, lastA);
    Serial.println("TEST done");
  }
  delay(1);
}
