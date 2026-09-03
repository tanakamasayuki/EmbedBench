// X17 redone per review: the device's UART replies go through the core's
// RX sink, which records a dev.tx event before pushRx, so the moment the
// device transmitted is part of the event list. Covers both the immediate
// reply (from the kUartTx notification) and the tick-delayed reply
// (delivered by the director). All structures are experiment-local.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostClock.h>
#include <HostUart.h>
#include <stdio.h>
#include <string.h>

using namespace HostArduino;

static const uint32_t kTickUs = 1000;
static char trace[512];
static size_t traceLen = 0;
static uint32_t seq = 0;
static uint64_t virtualNowUs = 0;
static uint64_t nextTickUs = kTickUs;
static uint32_t waitCalls = 0;
static char pendingReply[8] = {0};
static uint64_t pendingDueUs = 0;

static void record(const char* detail) {
  ++seq;
  traceLen += snprintf(trace + traceLen, sizeof(trace) - traceLen,
                       "%02u %06llu %s\n", seq,
                       static_cast<unsigned long long>(virtualNowUs), detail);
}

// The core UART RX sink: every device reply is recorded here first, so
// "when the device transmitted" survives into the event list, then pushed.
static void uartRxSink(const char* bytes) {
  char detail[32];
  snprintf(detail, sizeof(detail), "dev.tx %s", bytes);
  record(detail);
  Serial1.pushRx(bytes);
}

static void onActivity(HostUart::ActivityEvent event, HostUart&,
                       const uint8_t* data, size_t len, void*) {
  char detail[48];
  switch (event) {
    case HostUart::kUartBegin:
      record("uart.begin");
      break;
    case HostUart::kUartTx: {
      snprintf(detail, sizeof(detail), "uart.tx %.*s", static_cast<int>(len),
               reinterpret_cast<const char*>(data));
      record(detail);
      // Device model: "AT" answers on the wire at once, through the sink;
      // any longer AT command answers one tick later, also through the
      // sink when the director delivers it.
      if (len == 2 && data[0] == 'A' && data[1] == 'T') {
        uartRxSink("OK");
      } else if (len > 2 && data[0] == 'A' && data[1] == 'T') {
        snprintf(pendingReply, sizeof(pendingReply), "OK");
        pendingDueUs = virtualNowUs + kTickUs;
      }
      break;
    }
    case HostUart::kUartRx:
      snprintf(detail, sizeof(detail), "uart.rx %c", data[0]);
      record(detail);
      break;
    default:
      break;
  }
}

static uint64_t onNow(void*) { return virtualNowUs; }

static void onWait(uint32_t us, void*) {
  ++waitCalls;
  const uint64_t target = virtualNowUs + us;
  while (nextTickUs <= target) {
    virtualNowUs = nextTickUs;
    nextTickUs += kTickUs;
    if (pendingReply[0] != '\0' && virtualNowUs >= pendingDueUs) {
      uartRxSink(pendingReply);
      pendingReply[0] = '\0';
    }
  }
  if (target > virtualNowUs) virtualNowUs = target;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start uart_sink");

  Serial1.setActivityHook(&onActivity);
  setClockHooks(&onNow, &onWait);
  Serial1.begin(9600);
  Serial1.setTimeout(10);

  const uint64_t t1 = virtualNowUs;
  const uint32_t waits1 = waitCalls;
  Serial1.print("AT");
  uint8_t reply1[2] = {0};
  Serial1.readBytes(reply1, sizeof(reply1));
  const uint64_t elapsed1 = virtualNowUs - t1;
  const uint32_t waitDelta1 = waitCalls - waits1;

  delay(2);

  const uint64_t t2 = virtualNowUs;
  Serial1.print("AT+S");
  uint8_t reply2[2] = {0};
  Serial1.readBytes(reply2, sizeof(reply2));
  const uint64_t elapsed2 = virtualNowUs - t2;

  clearClockHooks();
  Serial1.clearActivityHook();
  Serial1.end();

  Serial.print(trace);
  Serial.printf("ex1 rx=%c%c elapsed=%llu waits=%u\n", reply1[0], reply1[1],
                static_cast<unsigned long long>(elapsed1), waitDelta1);
  Serial.printf("ex2 rx=%c%c elapsed=%llu\n", reply2[0], reply2[1],
                static_cast<unsigned long long>(elapsed2));
  Serial.println("TEST done");
}

void loop() { delay(10); }
