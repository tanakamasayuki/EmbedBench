// Quantifies host-core paths that cannot currently be observed or fired.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>

static volatile uint32_t interruptCalls = 0;
static uint32_t analogReadHookCalls = 0;

static void onInterrupt() { ++interruptCalls; }

static uint16_t onAnalogRead(uint8_t, uint16_t held, void*) {
  ++analogReadHookCalls;
  return held;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start host_gaps");

  const uint8_t interruptPin = 9;
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), &onInterrupt, CHANGE);
  HostArduino::setPinValue(interruptPin, LOW);
  HostArduino::setPinValue(interruptPin, HIGH);
  Serial.printf("interrupt_edges=2 callback_calls=%u\n",
                static_cast<unsigned>(interruptCalls));
  detachInterrupt(digitalPinToInterrupt(interruptPin));

  const uint8_t analogPin = 10;
  HostArduino::setAnalogValue(analogPin, 1234);
  HostArduino::setAnalogMilliVolts(analogPin, 3300);
  HostArduino::setAnalogReadHook(&onAnalogRead);
  const uint16_t raw = analogRead(analogPin);
  const uint32_t afterRaw = analogReadHookCalls;
  const uint32_t millivolts = analogReadMilliVolts(analogPin);
  const uint32_t afterMillivolts = analogReadHookCalls;
  HostArduino::clearAnalogHooks();
  Serial.printf("analog_raw=%u mv=%u hook_after_raw=%u hook_after_mv=%u\n", raw,
                millivolts, afterRaw, afterMillivolts);

  Serial1.begin(9600);
  const uint32_t totalBefore = Serial1.txTotal();
  Serial1.write(static_cast<uint8_t>('A'));
  Serial1.write(static_cast<uint8_t>('T'));
  const uint32_t totalAfter = Serial1.txTotal();
  const size_t queued = Serial1.txAvailable();
  uint8_t drained[2] = {0};
  const size_t drainedCount = Serial1.readTx(drained, sizeof(drained));
  Serial.printf("uart_tx_delta=%u queued=%u drained=%u bytes=%c%c activity_hook=0\n",
                totalAfter - totalBefore, static_cast<unsigned>(queued),
                static_cast<unsigned>(drainedCount), drained[0], drained[1]);

  Serial.println("TEST done");
}

void loop() { delay(10); }

