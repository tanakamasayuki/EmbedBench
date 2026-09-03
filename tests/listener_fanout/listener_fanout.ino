// Measures a minimal experiment-local dispatcher that owns the single
// host pin-write hook and fans events out to a fixed number of listener
// slots: capacity, rejection at the cap, and removal during a callback.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <string.h>

using namespace HostArduino;

using PinListener = void (*)(uint8_t pin, uint8_t value, void* user);

constexpr size_t kMaxListeners = 4;

struct Slot {
  PinListener fn = nullptr;
  void* user = nullptr;
};

struct Dispatcher {
  Slot slots[kMaxListeners];
  uint32_t events = 0;        // counted centrally even with zero listeners
  uint32_t rejectedAdds = 0;  // diagnostics when the table is full
};

static Dispatcher dispatcher;
static char order[16];
static size_t orderLen = 0;

static bool addListener(PinListener fn, void* user) {
  for (size_t i = 0; i < kMaxListeners; ++i) {
    if (dispatcher.slots[i].fn == nullptr) {
      dispatcher.slots[i].fn = fn;
      dispatcher.slots[i].user = user;
      return true;
    }
  }
  ++dispatcher.rejectedAdds;
  return false;
}

static bool removeListener(PinListener fn) {
  for (size_t i = 0; i < kMaxListeners; ++i) {
    if (dispatcher.slots[i].fn == fn) {
      dispatcher.slots[i].fn = nullptr;
      dispatcher.slots[i].user = nullptr;
      return true;
    }
  }
  return false;
}

static void onHostPinWrite(uint8_t pin, uint8_t value, void*) {
  ++dispatcher.events;
  // Index iteration over stable slots keeps dispatch safe when a callback
  // clears a slot mid-event; a cleared later slot is simply skipped.
  for (size_t i = 0; i < kMaxListeners; ++i) {
    if (dispatcher.slots[i].fn != nullptr) {
      dispatcher.slots[i].fn(pin, value, dispatcher.slots[i].user);
    }
  }
}

static void appendTag(char tag) {
  if (orderLen < sizeof(order) - 1) {
    order[orderLen++] = tag;
    order[orderLen] = '\0';
  }
}

static uint32_t callsA = 0, callsB = 0, callsC = 0, callsD = 0, callsE = 0;
static uint32_t callsS = 0, callsR = 0;

static void listenerA(uint8_t, uint8_t, void*) { ++callsA; appendTag('A'); }
static void listenerB(uint8_t, uint8_t, void*) { ++callsB; appendTag('B'); }
static void listenerC(uint8_t, uint8_t, void*) { ++callsC; appendTag('C'); }
static void listenerD(uint8_t, uint8_t, void*) { ++callsD; appendTag('D'); }
static void listenerE(uint8_t, uint8_t, void*) { ++callsE; appendTag('E'); }

static void listenerSelfRemove(uint8_t, uint8_t, void*) {
  ++callsS;
  appendTag('S');
  removeListener(&listenerSelfRemove);
}

static void listenerRemoveLater(uint8_t, uint8_t, void*) {
  ++callsR;
  appendTag('R');
  removeListener(&listenerD);
}

static void fire() {
  order[0] = '\0';
  orderLen = 0;
  digitalWrite(4, HIGH);
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start listener_fanout");

  pinMode(4, OUTPUT);
  setPinWriteHook(&onHostPinWrite);

  // Phase 0: zero listeners; the event is still counted centrally.
  fire();
  Serial.printf("p0_events=%u order=<%s>\n", dispatcher.events, order);

  // Phase 1: one listener.
  addListener(&listenerA, nullptr);
  fire();
  Serial.printf("p1_a=%u order=<%s>\n", callsA, order);

  // Phase 2: fill every slot; dispatch order is slot order.
  addListener(&listenerB, nullptr);
  addListener(&listenerC, nullptr);
  addListener(&listenerD, nullptr);
  fire();
  Serial.printf("p2_a=%u b=%u c=%u d=%u order=<%s>\n", callsA, callsB, callsC,
                callsD, order);

  // Phase 3: a fifth registration is rejected and diagnosed.
  const bool added = addListener(&listenerE, nullptr);
  fire();
  Serial.printf("p3_added=%d rejected=%u e=%u order=<%s>\n", added ? 1 : 0,
                dispatcher.rejectedAdds, callsE, order);

  // Phase 4: plain removal frees the slot; the others keep firing.
  removeListener(&listenerB);
  fire();
  Serial.printf("p4_b=%u order=<%s>\n", callsB, order);

  // Phase 5: a listener that removes itself mid-callback; later slots on
  // the same event still fire, and the next event skips it.
  addListener(&listenerSelfRemove, nullptr);
  fire();
  Serial.printf("p5_first_order=<%s>\n", order);
  fire();
  Serial.printf("p5_second_s=%u order=<%s>\n", callsS, order);

  // Phase 6: a listener that removes a later listener mid-callback; the
  // removed listener misses the current event.
  addListener(&listenerRemoveLater, nullptr);
  const uint32_t dBefore = callsD;
  fire();
  Serial.printf("p6_d_delta=%u order=<%s>\n", callsD - dBefore, order);

  setPinWriteHook(nullptr);
  Serial.printf("state_bytes=%u events=%u\n",
                static_cast<unsigned>(sizeof(dispatcher)), dispatcher.events);
  Serial.println("TEST done");
}

void loop() { delay(10); }
