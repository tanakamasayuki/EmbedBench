// Vertical slice for interrupts using the 1.7.1 port: a candidate
// injection path captures the line change, decides the edge against the
// normalized trigger, records the event, invokes the registered ISR, and
// tags the ISR's own bus traffic with ctx=isr via the enter/exit bracket.
// All structures are experiment-local candidates.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostInterrupt.h>
#include <stdio.h>
#include <string.h>

using namespace HostArduino;

static char trace[512];
static size_t traceLen = 0;
static uint32_t seq = 0;
static uint32_t isrDepth = 0;

static void record(const char* origin, const char* detail) {
  ++seq;
  const char* ctx = isrDepth > 0 ? "isr" : "main";
  traceLen += snprintf(trace + traceLen, sizeof(trace) - traceLen,
                       "%02u %s %s %s\n", seq, ctx, origin, detail);
}

// Candidate injection path: record first, then apply, then decide the
// edge against the normalized trigger and invoke the registered ISR.
// Matching on InterruptTrigger, never the raw mode (X13).
static void injectLevel(uint8_t pin, uint8_t level) {
  const uint8_t previous = pinValue(pin);
  const InterruptTrigger trigger = interruptTrigger(pin);
  bool match = false;
  if (previous == LOW && level == HIGH) {
    match = trigger == kTriggerRising || trigger == kTriggerChange;
  } else if (previous == HIGH && level == LOW) {
    match = trigger == kTriggerFalling || trigger == kTriggerChange;
  }
  char detail[48];
  snprintf(detail, sizeof(detail), "inject pin=%u %u->%u match=%d", pin,
           previous, level, match ? 1 : 0);
  record("dir", detail);
  setPinValue(pin, level);
  if (match) triggerInterrupt(pin);
}

static void onInterruptEvent(InterruptEvent event, const InterruptSlot& slot,
                             void*) {
  char detail[32];
  if (event == kInterruptEnter) {
    ++isrDepth;
    snprintf(detail, sizeof(detail), "isr.enter pin=%u", slot.pin);
    record("core", detail);
  } else if (event == kInterruptExit) {
    snprintf(detail, sizeof(detail), "isr.exit pin=%u", slot.pin);
    record("core", detail);
    if (isrDepth > 0) --isrDepth;
  }
}

static void onPinWrite(uint8_t pin, uint8_t value, void*) {
  char detail[32];
  snprintf(detail, sizeof(detail), "gpio.write pin=%u val=%u", pin, value);
  record("app", detail);
}

// The application ISR: ordinary sketch code that touches the bus.
static void appIsr() { digitalWrite(5, HIGH); }

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start interrupt_slice");

  // Setup phase (unrecorded, route-3 rule): pin modes and registration.
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(27, INPUT);
  attachInterrupt(27, &appIsr, RISING);

  setInterruptHook(&onInterruptEvent);
  setPinWriteHook(&onPinWrite);

  // Run: app traffic, a matching edge, a non-matching edge, app traffic.
  digitalWrite(4, HIGH);
  injectLevel(27, HIGH);  // rising, matches -> ISR runs, tagged ctx=isr
  injectLevel(27, LOW);   // falling, no match -> injection event only
  digitalWrite(4, LOW);

  setPinWriteHook(nullptr);
  clearInterruptHook();

  Serial.print(trace);
  uint32_t isrTagged = 0;
  for (const char* p = trace; (p = strstr(p, " isr ")) != nullptr; ++p) {
    ++isrTagged;
  }
  Serial.printf("fires=%u isr_tagged=%u events=%u\n", interruptSlot(27).fires,
                isrTagged, seq);

  resetInterrupts();
  Serial.println("TEST done");
}

void loop() { delay(10); }
