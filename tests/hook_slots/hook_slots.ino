// Confirms which host hook registrations replace an existing consumer.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostClock.h>
#include <HostLifecycle.h>

using namespace HostArduino;

static uint32_t pinCallsA = 0;
static uint32_t pinCallsB = 0;
static uint32_t lifecycleCallsA = 0;
static uint32_t lifecycleCallsB = 0;
static uint32_t clockWaitCallsA = 0;
static uint32_t clockWaitCallsB = 0;
static uint64_t clockA = 0;
static uint64_t clockB = 0;
static bool reported = false;

static void onPinA(uint8_t, uint8_t, void*) { ++pinCallsA; }
static void onPinB(uint8_t, uint8_t, void*) { ++pinCallsB; }
static void onLifecycleA(LifecyclePhase, void*) { ++lifecycleCallsA; }
static void onLifecycleB(LifecyclePhase, void*) { ++lifecycleCallsB; }
static uint64_t onNowA(void*) { return clockA; }
static uint64_t onNowB(void*) { return clockB; }
static void onWaitA(uint32_t us, void*) {
  clockA += us;
  ++clockWaitCallsA;
}
static void onWaitB(uint32_t us, void*) {
  clockB += us;
  ++clockWaitCallsB;
}

struct HookInstaller {
  HookInstaller() {
    setLifecycleHook(&onLifecycleA);
    setLifecycleHook(&onLifecycleB);
  }
};

static HookInstaller installer;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start hook_slots");

  setPinWriteHook(&onPinA);
  digitalWrite(4, HIGH);
  setPinWriteHook(&onPinB);
  digitalWrite(4, LOW);
  clearPinHooks();
  digitalWrite(4, HIGH);
  Serial.printf("pin_a=%u pin_b=%u physical_writes=3\n", pinCallsA, pinCallsB);

  setClockHooks(&onNowA, &onWaitA);
  setClockHooks(&onNowB, &onWaitB);
  delayMicroseconds(7);
  Serial.printf("clock_a_calls=%u clock_b_calls=%u clock_a_us=%llu clock_b_us=%llu\n",
                clockWaitCallsA, clockWaitCallsB,
                static_cast<unsigned long long>(clockA),
                static_cast<unsigned long long>(clockB));
  clearClockHooks();
}

void loop() {
  if (!reported) {
    reported = true;
    Serial.printf("lifecycle_a=%u lifecycle_b_before_post=%u\n", lifecycleCallsA,
                  lifecycleCallsB);
    Serial.println("TEST done");
  }
  clockRealWaitMicros(10000);
}

