// Compares the candidate policies for completing an event that carries a
// response, in the case that matters: the device's response callback
// itself calls a core sink (an IRQ line) and generates a nested event.
//   two_lines  request and response as two events
//   reserve    seq/timestamp reserved at receipt, completed after response
//   forbid     sink calls inside a response callback are rejected
//   defer      sink calls inside a response callback apply after the op
// All structures are experiment-local candidates.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

using namespace HostArduino;

enum class Policy : uint8_t { kTwoLines, kReserve, kForbid, kDefer };

static Policy policy = Policy::kTwoLines;

// Slot-based log: slot order is seq order; completion may happen later.
struct Slot {
  uint32_t seq = 0;
  bool complete = false;
  char text[44] = {0};
};

static Slot slots[8];
static size_t slotCount = 0;
static uint32_t nextSeq = 1;
static char streamOrder[16];  // completion (streaming) order as seq digits
static size_t streamLen = 0;
static uint32_t incompleteExposure = 0;
static uint32_t rejectedSinkCalls = 0;
static uint32_t deferredSinkCalls = 0;
static bool inResponse = false;
static bool hasPending = false;
static uint8_t pendingPin = 0;
static uint8_t pendingLevel = 0;
static uint8_t pinDuringCallback = 0xFF;

static size_t reserveSlot() {
  Slot& slot = slots[slotCount];
  slot.seq = nextSeq++;
  slot.complete = false;
  slot.text[0] = '\0';
  return slotCount++;
}

static void completeSlot(size_t index, const char* text) {
  snprintf(slots[index].text, sizeof(slots[index].text), "%s", text);
  slots[index].complete = true;
  if (streamLen < sizeof(streamOrder) - 1) {
    streamOrder[streamLen++] = static_cast<char>('0' + slots[index].seq);
    streamOrder[streamLen] = '\0';
  }
  for (size_t i = 0; i < index; ++i) {
    if (!slots[i].complete) ++incompleteExposure;
  }
}

static void record(const char* text) { completeSlot(reserveSlot(), text); }

// The core GPIO sink a device model uses for its IRQ / DRDY lines.
static bool gpioSink(uint8_t pin, uint8_t level) {
  if (inResponse && policy == Policy::kForbid) {
    ++rejectedSinkCalls;
    return false;
  }
  if (inResponse && policy == Policy::kDefer) {
    ++deferredSinkCalls;
    hasPending = true;
    pendingPin = pin;
    pendingLevel = level;
    return true;
  }
  char text[40];
  snprintf(text, sizeof(text), "gpio.inject pin=%u val=%u", pin, level);
  record(text);
  setPinValue(pin, level);
  return true;
}

// Device model: answers one byte and raises its IRQ line while doing so.
static size_t deviceRead(uint8_t* data, size_t len) {
  if (len < 1) return 0;
  data[0] = 0x2C;
  gpioSink(7, HIGH);
  pinDuringCallback = pinValue(7);
  return 1;
}

static size_t onWireRead(uint8_t address, uint8_t* data, size_t len, bool,
                         void*) {
  char text[44];
  size_t count = 0;
  switch (policy) {
    case Policy::kTwoLines: {
      snprintf(text, sizeof(text), "i2c.req addr=%02X req=%u", address,
               static_cast<unsigned>(len));
      record(text);
      inResponse = true;
      count = deviceRead(data, len);
      inResponse = false;
      snprintf(text, sizeof(text), "i2c.resp addr=%02X data=%02X", address,
               data[0]);
      record(text);
      break;
    }
    case Policy::kReserve: {
      const size_t slot = reserveSlot();
      inResponse = true;
      count = deviceRead(data, len);
      inResponse = false;
      snprintf(text, sizeof(text), "i2c.read addr=%02X data=%02X", address,
               data[0]);
      completeSlot(slot, text);
      break;
    }
    case Policy::kForbid:
    case Policy::kDefer: {
      inResponse = true;
      count = deviceRead(data, len);
      inResponse = false;
      snprintf(text, sizeof(text), "i2c.read addr=%02X data=%02X", address,
               data[0]);
      record(text);
      if (policy == Policy::kForbid && rejectedSinkCalls > 0) {
        record("diag.reject sink_in_response");
      }
      if (policy == Policy::kDefer && hasPending) {
        hasPending = false;
        snprintf(text, sizeof(text), "gpio.inject pin=%u val=%u deferred=1",
                 pendingPin, pendingLevel);
        record(text);
        setPinValue(pendingPin, pendingLevel);
      }
      break;
    }
  }
  return count;
}

static void runScenario(const char* name, Policy p) {
  policy = p;
  slotCount = 0;
  nextSeq = 1;
  streamOrder[0] = '\0';
  streamLen = 0;
  incompleteExposure = 0;
  rejectedSinkCalls = 0;
  deferredSinkCalls = 0;
  inResponse = false;
  hasPending = false;
  pinDuringCallback = 0xFF;
  setPinValue(7, LOW);

  Wire.requestFrom(static_cast<uint16_t>(0x48), static_cast<size_t>(1), true);
  Wire.read();

  bool streamInSeqOrder = true;
  for (size_t i = 1; i < streamLen; ++i) {
    if (streamOrder[i] < streamOrder[i - 1]) streamInSeqOrder = false;
  }
  Serial.printf(
      "== %s lines=%u stream=%s in_order=%d exposure=%u rejected=%u "
      "deferred=%u pin_cb=%u pin_final=%u\n",
      name, static_cast<unsigned>(slotCount), streamOrder,
      streamInSeqOrder ? 1 : 0, incompleteExposure, rejectedSinkCalls,
      deferredSinkCalls, pinDuringCallback, pinValue(7));
  for (size_t i = 0; i < slotCount; ++i) {
    Serial.printf("log %u %s\n", slots[i].seq, slots[i].text);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start event_timing");

  Wire.begin(21, 22, 400000);
  Wire.setReadHook(&onWireRead);

  runScenario("two_lines", Policy::kTwoLines);
  runScenario("reserve", Policy::kReserve);
  runScenario("forbid", Policy::kForbid);
  runScenario("defer", Policy::kDefer);

  Wire.clearHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }
