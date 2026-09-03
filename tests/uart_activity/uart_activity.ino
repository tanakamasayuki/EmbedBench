// Verifies the host core 1.7.1 UART activity hook against the H2 request:
// begin/end/config observed, TX reported before write() returns so UART
// traffic keeps its order among GPIO events, an immediate reply pushed
// from the TX notification (no wait needed), per-byte RX consumption
// events, flush() discard reporting, and hook/queue coexistence.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostClock.h>
#include <HostUart.h>

using namespace HostArduino;

static char trace[64];
static size_t traceLen = 0;
static uint64_t virtualNowUs = 0;
static uint32_t waitCalls = 0;

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

static void onActivity(HostUart::ActivityEvent event, HostUart& uart,
                       const uint8_t* data, size_t len, void*) {
  switch (event) {
    case HostUart::kUartBegin:
      appendTag('B');
      break;
    case HostUart::kUartEnd:
      appendTag('N');
      break;
    case HostUart::kUartConfig:
      appendTag('C');
      break;
    case HostUart::kUartTx:
      appendTag('T');
      appendTag(static_cast<char>('0' + len));
      // The reply is pushed from inside the notification, before the
      // sketch's write() has returned.
      if (len >= 2 && data[0] == 'A' && data[1] == 'T') {
        uart.pushRx("OK");
      }
      break;
    case HostUart::kUartRx:
      appendTag('R');
      break;
    case HostUart::kUartRxDiscard:
      appendTag('F');
      appendTag(static_cast<char>('0' + len));
      break;
  }
}

static void onPinWrite(uint8_t, uint8_t, void*) { appendTag('w'); }

static uint64_t onNow(void*) { return virtualNowUs; }
static void onWait(uint32_t us, void*) {
  ++waitCalls;
  virtualNowUs += us;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start uart_activity");

  Serial1.setActivityHook(&onActivity);
  setPinWriteHook(&onPinWrite);

  // begin() is an event, and the instance identifies its port.
  resetTrace();
  Serial1.begin(9600);
  Serial.printf("begin trace=<%s> uart_num=%u\n", trace, Serial1.uartNum());

  // Configuration changes are events too.
  resetTrace();
  Serial1.updateBaudRate(115200);
  Serial.printf("config trace=<%s>\n", trace);

  // The TX notification fires between the surrounding GPIO events, which
  // is the ordering that queue polling could not preserve (X3/X6). The
  // bytes also still enter the tx queue: watching and polling coexist.
  resetTrace();
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  Serial1.print("AT");
  digitalWrite(4, LOW);
  Serial.printf("tx_order trace=<%s> tx_avail=%d\n", trace,
                Serial1.txAvailable());

  // The reply pushed from the hook is already queued, so the blocking
  // read consumes it with zero wait calls (X3 needed one 1,000 us wait).
  resetTrace();
  setClockHooks(&onNow, &onWait);
  Serial1.setTimeout(10);
  uint8_t reply[2] = {0};
  const size_t replyLen = Serial1.readBytes(reply, sizeof(reply));
  clearClockHooks();
  Serial.printf("reply len=%u rx=%c%c wait_calls=%u trace=<%s>\n",
                static_cast<unsigned>(replyLen), reply[0], reply[1], waitCalls,
                trace);

  // The driver's own drain is its side of the wire and reports nothing.
  resetTrace();
  uint8_t drained[4] = {0};
  const size_t drainedLen = Serial1.readTx(drained, sizeof(drained));
  Serial.printf("drain len=%u bytes=%c%c trace=<%s>\n",
                static_cast<unsigned>(drainedLen), drained[0], drained[1],
                trace);

  // flush() reports what it drops unread, so the trace keeps no hole.
  resetTrace();
  Serial1.pushRx("junk");
  Serial1.flush();
  Serial.printf("discard trace=<%s>\n", trace);

  resetTrace();
  Serial1.end();
  Serial.printf("end trace=<%s>\n", trace);

  Serial1.clearActivityHook();
  clearPinHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }
