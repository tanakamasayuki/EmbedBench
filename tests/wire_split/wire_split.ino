// Splits the single Wire host hooks into side-effect-free observers plus
// exactly one responder device per address, so the status byte and read
// bytes always have a single source. Experiment-local candidate only.
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <string.h>

constexpr size_t kMaxDevices = 2;

struct Observer {
  uint32_t writesSeen = 0;
  uint32_t readsSeen = 0;
  uint16_t lastAddress = 0;
};

// Observers only watch; their notification path has no return value, so
// they cannot influence the status or the bytes the sketch receives.
static Observer observerA;
static Observer observerB;

struct Device {
  bool used = false;
  uint16_t address = 0;
  uint8_t lastWrite[4] = {0};
  size_t lastWriteLength = 0;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;
};

static Device devices[kMaxDevices];
static uint32_t diagUnboundWrite = 0;
static uint32_t diagUnboundRead = 0;
static uint32_t diagDuplicateBind = 0;

static Device* findDevice(uint16_t address) {
  for (size_t i = 0; i < kMaxDevices; ++i) {
    if (devices[i].used && devices[i].address == address) return &devices[i];
  }
  return nullptr;
}

static bool bindDevice(size_t index, uint16_t address) {
  if (findDevice(address) != nullptr) {
    ++diagDuplicateBind;
    return false;
  }
  devices[index].used = true;
  devices[index].address = address;
  return true;
}

static void notifyWrite(uint16_t address) {
  ++observerA.writesSeen;
  observerA.lastAddress = address;
  ++observerB.writesSeen;
  observerB.lastAddress = address;
}

static void notifyRead(uint16_t address) {
  ++observerA.readsSeen;
  observerA.lastAddress = address;
  ++observerB.readsSeen;
  observerB.lastAddress = address;
}

static uint8_t onWireWrite(uint8_t address, const uint8_t* data, size_t len,
                           bool, void*) {
  notifyWrite(address);
  Device* device = findDevice(address);
  if (device == nullptr) {
    ++diagUnboundWrite;
    return 2;  // NACK on address, Arduino endTransmission convention
  }
  device->lastWriteLength = len < sizeof(device->lastWrite)
                                ? len
                                : sizeof(device->lastWrite);
  for (size_t i = 0; i < device->lastWriteLength; ++i) {
    device->lastWrite[i] = data[i];
  }
  ++device->writeCalls;
  return 0;
}

static size_t onWireRead(uint8_t address, uint8_t* data, size_t len, bool,
                         void*) {
  notifyRead(address);
  Device* device = findDevice(address);
  if (device == nullptr) {
    ++diagUnboundRead;
    return 0;
  }
  const size_t count = len < device->lastWriteLength ? len
                                                     : device->lastWriteLength;
  // Reply with each stored byte incremented so the source is verifiable.
  for (size_t i = 0; i < count; ++i) data[i] = device->lastWrite[i] + 1;
  ++device->readCalls;
  return count;
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start wire_split");

  Wire.begin(21, 22, 400000);
  Wire.setWriteHook(&onWireWrite);
  Wire.setReadHook(&onWireRead);

  // Binding: the second device asking for the same address is refused, so
  // at most one responder exists per address.
  const bool bindFirst = bindDevice(0, 0x34);
  const bool bindDuplicate = bindDevice(1, 0x34);
  Serial.printf("bind_first=%d bind_duplicate=%d diag_dup=%u\n",
                bindFirst ? 1 : 0, bindDuplicate ? 1 : 0, diagDuplicateBind);

  // Case 1: write to the bound address; the device alone decides status 0
  // while both observers see the same transaction.
  Wire.beginTransmission(0x34);
  Wire.write(0xAB);
  Wire.write(0xCD);
  const uint8_t status = Wire.endTransmission();
  Serial.printf("case1_status=%u dev_writes=%u obs_a_w=%u obs_b_w=%u\n",
                status, devices[0].writeCalls, observerA.writesSeen,
                observerB.writesSeen);

  // Case 2: read from the bound address; the bytes come from the single
  // responder and both observers count the read.
  const size_t received =
      Wire.requestFrom(static_cast<uint16_t>(0x34), static_cast<size_t>(2), true);
  const int first = Wire.read();
  const int second = Wire.read();
  Serial.printf("case2_len=%u rx=%02X%02X obs_a_r=%u obs_b_r=%u\n",
                static_cast<unsigned>(received), first, second,
                observerA.readsSeen, observerB.readsSeen);

  // Case 3: write to an unbound address; the dispatcher alone supplies the
  // failure status, the observers still see the attempt, and a diagnostic
  // records that no responder was bound.
  Wire.beginTransmission(0x55);
  Wire.write(0x11);
  const uint8_t unboundStatus = Wire.endTransmission();
  Serial.printf("case3_status=%u diag_w=%u obs_a_w=%u\n", unboundStatus,
                diagUnboundWrite, observerA.writesSeen);

  // Case 4: read from an unbound address returns zero bytes and diagnoses.
  const size_t unboundReceived =
      Wire.requestFrom(static_cast<uint16_t>(0x55), static_cast<size_t>(2), true);
  Serial.printf("case4_len=%u diag_r=%u obs_a_r=%u\n",
                static_cast<unsigned>(unboundReceived), diagUnboundRead,
                observerA.readsSeen);

  // Case 5: after the refused duplicate bind, the original responder still
  // answers alone.
  Wire.beginTransmission(0x34);
  Wire.write(0xEE);
  const uint8_t statusAgain = Wire.endTransmission();
  Serial.printf("case5_status=%u dev0_writes=%u dev1_writes=%u\n", statusAgain,
                devices[0].writeCalls, devices[1].writeCalls);

  Wire.clearHooks();
  Serial.println("TEST done");
}

void loop() { delay(10); }
