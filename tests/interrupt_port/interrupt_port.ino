// Verifies the host core 1.7.1 interrupt port against the H1 request:
// registration observed, no self-firing on pin changes, synchronous
// invocation with enter/exit bracketing, nesting depth, self-detach, the
// argument spelling, fire counts across re-arm, and the raw-mode
// numbering trap (RISING/CHANGE raw values differ from arduino-esp32).
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostInterrupt.h>

using namespace HostArduino;

static char trace[64];
static size_t traceLen = 0;
static uint32_t maxDepth = 0;
static uint32_t detachEvents = 0;

static void appendTag(char tag) {
  if (traceLen < sizeof(trace) - 1) {
    trace[traceLen++] = tag;
    trace[traceLen] = '\0';
  }
}

static void resetTrace() {
  trace[0] = '\0';
  traceLen = 0;
}

static void onInterruptEvent(InterruptEvent event, const InterruptSlot& slot,
                             void*) {
  switch (event) {
    case kInterruptAttach:
      appendTag('A');
      break;
    case kInterruptDetach:
      appendTag('D');
      ++detachEvents;
      break;
    case kInterruptEnter:
      appendTag('E');
      appendTag(static_cast<char>('0' + slot.depth));
      if (slot.depth > maxDepth) maxDepth = slot.depth;
      break;
    case kInterruptExit:
      appendTag('X');
      appendTag(static_cast<char>('0' + slot.depth));
      break;
  }
}

static void onPinWrite(uint8_t, uint8_t, void*) { appendTag('w'); }

static void handlerWritesPin() { digitalWrite(5, HIGH); }

static bool nestedOnce = false;
static void handlerNests() {
  if (!nestedOnce) {
    nestedOnce = true;
    triggerInterrupt(33);
  }
}

static void handlerSelfDetach() { detachInterrupt(12); }

static void handlerWithArg(void* arg) {
  ++(*static_cast<uint32_t*>(arg));
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start interrupt_port");

  setInterruptHook(&onInterruptEvent);
  setPinWriteHook(&onPinWrite);

  // Registration is observed, and the raw mode constant is host-local:
  // RISING is 3 here while arduino-esp32 uses 1, so only the normalized
  // trigger is safe to match on.
  resetTrace();
  attachInterrupt(27, &handlerWritesPin, RISING);
  Serial.printf("attach mode_raw=%d trigger=%d trace=<%s>\n",
                interruptMode(27), static_cast<int>(interruptTrigger(27)),
                trace);

  // Pin movements alone fire nothing: the core holds the registration and
  // the edge decision stays with the layer above.
  setPinValue(27, HIGH);
  setPinValue(27, LOW);
  digitalWrite(27, HIGH);
  Serial.printf("no_auto_fire fires=%u\n", interruptSlot(27).fires);

  // Synchronous invocation, bracketed so the handler's own bus traffic is
  // identifiable as ISR-context in the trace.
  resetTrace();
  const bool fired = triggerInterrupt(27);
  Serial.printf("sync_fire ok=%d fires=%u trace=<%s>\n", fired ? 1 : 0,
                interruptSlot(27).fires, trace);

  // A handler that triggers again nests; depth makes it visible.
  attachInterrupt(33, &handlerNests, FALLING);
  resetTrace();
  triggerInterrupt(33);
  Serial.printf("nested fires=%u max_depth=%u trace=<%s>\n",
                interruptSlot(33).fires, maxDepth, trace);

  // A handler may detach itself; the pointer is taken before the call.
  attachInterrupt(12, &handlerSelfDetach, CHANGE);
  resetTrace();
  triggerInterrupt(12);
  const bool retrig = triggerInterrupt(12);
  Serial.printf("self_detach trace=<%s> retrig=%d attached=%d\n", trace,
                retrig ? 1 : 0, interruptAttached(12) ? 1 : 0);

  // The argument spelling, missing entirely before 1.7.1.
  static uint32_t argCount = 0;
  attachInterruptArg(14, &handlerWithArg, &argCount, FALLING);
  triggerInterrupt(14);
  Serial.printf("arg_handler count=%u mode_raw=%d trigger=%d\n", argCount,
                interruptMode(14), static_cast<int>(interruptTrigger(14)));

  // Re-attaching replaces the registration but keeps the fire count, and
  // reports only an attach event (no implicit detach).
  const uint32_t detachBefore = detachEvents;
  resetTrace();
  attachInterrupt(33, &handlerWritesPin, CHANGE);
  Serial.printf("rearm fires=%u mode_raw=%d trigger=%d trace=<%s> detach_delta=%u\n",
                interruptSlot(33).fires, interruptMode(33),
                static_cast<int>(interruptTrigger(33)), trace,
                detachEvents - detachBefore);

  // Unattached pins: trigger reports false, detach reports nothing.
  const uint32_t detachBefore2 = detachEvents;
  const bool unattached = triggerInterrupt(99);
  detachInterrupt(99);
  Serial.printf("unattached trig=%d detach_delta=%u\n", unattached ? 1 : 0,
                detachEvents - detachBefore2);

  resetInterrupts();
  clearInterruptHook();
  clearPinHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }
