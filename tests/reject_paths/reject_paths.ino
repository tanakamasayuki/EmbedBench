// Measures what the current host hooks show for operations the host
// rejects or drops: a Wire endTransmission without beginTransmission, a
// Wire transmit-buffer overflow, a UART tx-queue overflow, and refused
// analog calls. Grounds the Gate A choice on log-completeness scope.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostUart.h>
#include <Wire.h>

using namespace HostArduino;

static uint32_t wireWriteHookCalls = 0;
static uint32_t uartTxEvents = 0;
static uint32_t uartTxBytesSeen = 0;
static uint32_t analogEvents = 0;

static uint8_t onWireWrite(uint8_t, const uint8_t*, size_t, bool, void*) {
  ++wireWriteHookCalls;
  return 0;
}

static void onUartActivity(HostUart::ActivityEvent event, HostUart&,
                           const uint8_t*, size_t len, void*) {
  if (event == HostUart::kUartTx) {
    ++uartTxEvents;
    uartTxBytesSeen += len;
  }
}

static void onAnalogWrite(AnalogWriteEvent, const AnalogOut&, void*) {
  ++analogEvents;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start reject_paths");

  Wire.begin(21, 22, 400000);
  Wire.setWriteHook(&onWireWrite);

  // endTransmission with no matching beginTransmission: status 4, and the
  // write hook never fires.
  const uint8_t noBegin = Wire.endTransmission();
  Serial.printf("wire_no_begin status=%u hook_calls=%u\n", noBegin,
                wireWriteHookCalls);

  // Transmit-buffer overflow: the accepted count stops at the buffer
  // size, status is 1, and the model is never reached.
  Wire.beginTransmission(0x48);
  uint32_t accepted = 0;
  for (uint32_t i = 0; i < 200; ++i) {
    accepted += Wire.write(static_cast<uint8_t>(0xAA));
  }
  const uint8_t overflowStatus = Wire.endTransmission();
  Serial.printf("wire_overflow status=%u hook_calls=%u accepted=%u attempted=200\n",
                overflowStatus, wireWriteHookCalls, accepted);
  Wire.clearHooks();

  // UART tx-queue overflow: only accepted bytes reach the activity hook;
  // the loss is a sticky flag with no event, time, or count.
  Serial1.setActivityHook(&onUartActivity);
  Serial1.begin(9600);
  uint8_t chunk[100];
  memset(chunk, 0x55, sizeof(chunk));
  uint32_t written = 0;
  for (uint32_t i = 0; i < 12; ++i) {
    written += Serial1.write(chunk, sizeof(chunk));
  }
  Serial.printf("uart_overflow attempted=1200 written=%u hook_events=%u "
                "hook_bytes=%u overflow_flag=%d\n",
                written, uartTxEvents, uartTxBytesSeen,
                Serial1.txOverflowed() ? 1 : 0);
  Serial1.end();
  Serial1.clearActivityHook();

  // Refused analog calls change no state and report no event; a valid
  // attach afterwards shows the hook itself works.
  setAnalogWriteHook(&onAnalogWrite);
  const bool unattachedWrite = ledcWrite(33, 128);
  const bool zeroFreq = ledcAttach(33, 0, 8);
  const bool wideRes = ledcAttach(33, 5000, 24);
  const uint32_t rejectedEvents = analogEvents;
  const bool validAttach = ledcAttach(33, 5000, 8);
  Serial.printf("analog_reject unattached_write=%d zero_freq=%d wide_res=%d "
                "events_during_rejects=%u valid_attach=%d events_after=%u\n",
                unattachedWrite ? 1 : 0, zeroFreq ? 1 : 0, wideRes ? 1 : 0,
                rejectedEvents, validAttach ? 1 : 0, analogEvents);
  clearAnalogHooks();

  Serial.println("TEST done");
}

void loop() { delay(10); }
