// Measures the observation and response paths exposed by the host core.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostClock.h>
#include <SPI.h>
#include <Wire.h>

using namespace HostArduino;

static uint32_t pinWriteEvents = 0;
static uint32_t spiTransfers = 0;
static uint32_t spiTransactionEdges = 0;
static uint32_t wireWrites = 0;
static uint32_t wireReads = 0;
static uint16_t wireAddress = 0;
static uint8_t wirePayload[4] = {0};
static size_t wirePayloadLength = 0;
static uint64_t virtualNowUs = 0;
static uint32_t uartWaitCalls = 0;

static void onPinWrite(uint8_t, uint8_t, void*) { ++pinWriteEvents; }

static int invertPinRead(uint8_t, uint8_t held, void*) {
  return held ? LOW : HIGH;
}

static uint16_t halveAnalogRead(uint8_t, uint16_t held, void*) {
  return static_cast<uint16_t>(held / 2);
}

static uint8_t onSpiTransfer(uint8_t out, void*) {
  ++spiTransfers;
  return static_cast<uint8_t>(~out);
}

static void onSpiTransaction(bool, const SPISettings&, void*) {
  ++spiTransactionEdges;
}

static uint8_t onWireWrite(uint8_t address, const uint8_t* data, size_t len,
                           bool, void*) {
  ++wireWrites;
  wireAddress = address;
  wirePayloadLength = len < sizeof(wirePayload) ? len : sizeof(wirePayload);
  for (size_t i = 0; i < wirePayloadLength; ++i) wirePayload[i] = data[i];
  return 0;
}

static size_t onWireRead(uint8_t address, uint8_t* data, size_t len, bool, void*) {
  ++wireReads;
  wireAddress = address;
  const uint8_t response[] = {0x11, 0x22};
  const size_t count = len < sizeof(response) ? len : sizeof(response);
  for (size_t i = 0; i < count; ++i) data[i] = response[i];
  return count;
}

static uint64_t onNow(void*) { return virtualNowUs; }

static void onUartWait(uint32_t us, void*) {
  virtualNowUs += us;
  ++uartWaitCalls;
  uint8_t command[8] = {0};
  const size_t count = Serial1.readTx(command, sizeof(command));
  if (count >= 2 && command[0] == 'A' && command[1] == 'T') {
    Serial1.pushRx("OK");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start ports");

  const uint8_t gpio = 7;
  pinMode(gpio, OUTPUT);
  setPinWriteHook(&onPinWrite);
  digitalWrite(gpio, HIGH);
  digitalWrite(gpio, LOW);
  setPinValue(gpio, HIGH);
  const int injected = digitalRead(gpio);
  setPinReadHook(&invertPinRead);
  const int hooked = digitalRead(gpio);
  clearPinHooks();
  Serial.printf("gpio_write_events=%u injected=%d hooked=%d\n", pinWriteEvents,
                injected, hooked);

  const uint8_t analogPin = 8;
  setAnalogValue(analogPin, 1234);
  setAnalogMilliVolts(analogPin, 3300);
  const uint16_t analogInjected = analogRead(analogPin);
  setAnalogReadHook(&halveAnalogRead);
  const uint16_t analogHooked = analogRead(analogPin);
  clearAnalogHooks();
  Serial.printf("analog_raw=%u hooked=%u mv=%u\n", analogInjected, analogHooked,
                analogReadMilliVolts(analogPin));

  SPI.begin(18, 19, 23, 5);
  SPI.setTransferHook(&onSpiTransfer);
  SPI.setTransactionHook(&onSpiTransaction);
  SPI.beginTransaction(SPISettings(8000000, SPI_MSBFIRST, SPI_MODE0));
  const uint8_t spiReply = SPI.transfer(0xA5);
  SPI.endTransaction();
  Serial.printf("spi_reply=%02X transfers=%u edges=%u clock=%u mode=%u\n", spiReply,
                spiTransfers, spiTransactionEdges, SPI.settings().clock(),
                SPI.settings().dataMode());
  SPI.clearHooks();

  Wire.begin(21, 22, 400000);
  Wire.setWriteHook(&onWireWrite);
  Wire.setReadHook(&onWireRead);
  Wire.beginTransmission(0x34);
  Wire.write(0xAB);
  Wire.write(0xCD);
  const uint8_t wireStatus = Wire.endTransmission();
  const size_t wireReceived =
      Wire.requestFrom(static_cast<uint16_t>(0x34), static_cast<size_t>(2), true);
  const int wireFirst = Wire.read();
  const int wireSecond = Wire.read();
  Serial.printf("wire_status=%u writes=%u reads=%u addr=%02X tx_len=%u tx=%02X%02X rx_len=%u rx=%02X%02X\n",
                wireStatus, wireWrites, wireReads, wireAddress,
                static_cast<unsigned>(wirePayloadLength), wirePayload[0], wirePayload[1],
                static_cast<unsigned>(wireReceived), wireFirst, wireSecond);
  Wire.clearHooks();

  Serial1.begin(9600);
  Serial1.setTimeout(10);
  setClockHooks(&onNow, &onUartWait);
  const uint64_t uartBefore = virtualNowUs;
  Serial1.print("AT");
  uint8_t uartReply[2] = {0};
  const size_t uartReceived = Serial1.readBytes(uartReply, sizeof(uartReply));
  Serial.printf("uart_rx=%u reply=%c%c wait_calls=%u elapsed_us=%llu\n",
                static_cast<unsigned>(uartReceived), uartReply[0], uartReply[1],
                uartWaitCalls,
                static_cast<unsigned long long>(virtualNowUs - uartBefore));
  clearClockHooks();

  Serial.println("TEST done");
}

void loop() { delay(10); }
