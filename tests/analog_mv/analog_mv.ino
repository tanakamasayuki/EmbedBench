// Verifies the host core 1.7.1 analog observation additions against the
// H3 request: analogReadMilliVolts goes through its own hook with the
// held value, the raw AnalogReadHook stays independent, read-width
// configuration is observable, and clearAnalogHooks clears everything.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>

using namespace HostArduino;

static uint32_t mvCalls = 0;
static uint32_t rawCalls = 0;
static uint32_t heldSeen = 0;
static uint8_t configBits[4] = {0};
static uint32_t configCalls = 0;

static uint32_t halveMilliVolts(uint8_t, uint32_t held, void*) {
  ++mvCalls;
  heldSeen = held;
  return held / 2;
}

static uint16_t passRawRead(uint8_t, uint16_t held, void*) {
  ++rawCalls;
  return held;
}

static void onReadConfig(uint8_t bits, void*) {
  if (configCalls < sizeof(configBits)) configBits[configCalls] = bits;
  ++configCalls;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start analog_mv");

  const uint8_t pin = 8;
  setAnalogMilliVolts(pin, 3300);
  setAnalogValue(pin, 1000);

  // Baseline: no hook, the injected value comes straight back.
  Serial.printf("baseline mv=%u\n", analogReadMilliVolts(pin));

  // The mV read now has its own hook: it sees the held value and decides
  // what the sketch sees. The raw-read hook is untouched by it.
  setAnalogMilliVoltsHook(&halveMilliVolts);
  const uint32_t hookedMv = analogReadMilliVolts(pin);
  Serial.printf("hooked mv=%u held=%u mv_calls=%u raw_calls=%u\n", hookedMv,
                heldSeen, mvCalls, rawCalls);

  // The raw read keeps its own independent hook and count.
  setAnalogReadHook(&passRawRead);
  const uint16_t raw = analogRead(pin);
  Serial.printf("raw value=%u raw_calls=%u mv_calls=%u\n", raw, rawCalls,
                mvCalls);

  // Read-width changes are observable in call order; both spellings of
  // the same knob reach the same hook.
  setAnalogReadConfigHook(&onReadConfig);
  analogReadResolution(9);
  analogSetWidth(11);
  Serial.printf("config bits0=%u bits1=%u calls=%u\n", configBits[0],
                configBits[1], configCalls);

  // clearAnalogHooks clears all four analog hooks at once.
  clearAnalogHooks();
  const uint32_t clearedMv = analogReadMilliVolts(pin);
  analogReadResolution(10);
  Serial.printf("cleared mv=%u mv_calls=%u config_calls=%u\n", clearedMv,
                mvCalls, configCalls);

  Serial.println("TEST done");
}

void loop() { delay(10); }
