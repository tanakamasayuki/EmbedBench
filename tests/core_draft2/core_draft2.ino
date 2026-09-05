// Draft core v2: the three pieces the first round left out — analog
// observation and injection, a lifecycle-driven run window that the core
// closes on its own, and event listeners that watch the stream without
// being able to change it.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <embedbench_draft.h>
#include <stdio.h>
#include <string.h>

// --- Listeners: two observers plus one that removes itself -----------------
static uint32_t seenA = 0;
static uint32_t seenB = 0;
static uint32_t seenOnce = 0;
static char lastA[32] = {0};

static void listenerA(const ebd::Event& e, void*) {
  ++seenA;
  snprintf(lastA, sizeof(lastA), "%s", e.text);
}
static void listenerB(const ebd::Event&, void*) { ++seenB; }
static void listenerOnce(const ebd::Event&, void*) {
  ++seenOnce;
  ebd::removeListener(&listenerOnce);
}
static void listenerExtra(const ebd::Event&, void*) {}

// --- The run window is armed before main, the only point where the host
// core's lifecycle hook still catches preSetup.
struct WindowArmer {
  WindowArmer() { ebd::armRunWindow(1000, 2); }
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
  ebd::addListener(&listenerA);
  ebd::addListener(&listenerB);
  ebd::addListener(&listenerOnce);
  ebd::addListener(&listenerExtra);
  const bool fifth = ebd::addListener(&listenerA, reinterpret_cast<void*>(1));

  // Analog: the director injects held values, the application reads them,
  // and a PWM write is observed.
  ebd::analogInject(ebd::Origin::kDir, 8, 1234);
  ebd::analogInjectMilliVolts(ebd::Origin::kDir, 8, 3300);
  analogReadResolution(10);
  appRaw = analogRead(8);
  appMv = analogReadMilliVolts(8);
  analogWrite(9, 128);

  Serial.printf("values raw=%u mv=%u fifth=%d capacity=%u\n", appRaw, appMv,
                fifth ? 1 : 0,
                static_cast<unsigned>(ebd::listenerCapacity()));
}

void loop() {
  ++loopRuns;
  digitalWrite(4, loopRuns % 2 == 0 ? LOW : HIGH);
  if (loopRuns == 3) {
    // The window closed after the second loop: this third iteration must
    // leave no trace behind.
    static char trace[2048];
    ebd::formatTrace(trace, sizeof(trace));
    Serial.print(trace);
    const ebd::Stats s = ebd::stats();
    Serial.printf("stats events=%u dropped=%u closed=%d loops=%u\n", s.events,
                  s.dropped, ebd::runWindowClosed() ? 1 : 0,
                  ebd::completedLoops());
    Serial.printf("listeners a=%u b=%u once=%u last_a=%s\n", seenA, seenB,
                  seenOnce, lastA);
    Serial.println("TEST done");
  }
  delay(1);
}
