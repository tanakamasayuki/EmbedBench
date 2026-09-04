// Blocker check on the host core: the device's NUL-containing reply passes
// through HostPort::serialOut, the environment's uartInject, the host UART
// queue, and back into the application unchanged, and every log line stays
// intact (no C-string truncation).
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostUart.h>
#include <embedbench_draft.h>
#include <string.h>

#include <modem_model.h>

static AtModemModel modem;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebd::uartInject(ebd::Origin::kDev, data, len);
  }
};

static DraftPort draftPort;

static void devUartTx(const uint8_t* data, size_t len, void*) {
  modem.serialIn(data, len);
}
// [adapter end]

static uint8_t appReply[3] = {0xEE, 0xEE, 0xEE};
static size_t appGot = 0;

static void runOnce(char* out, size_t cap) {
  modem.reset();
  uint8_t drain[16];
  while (Serial1.readTx(drain, sizeof(drain)) > 0) {
  }
  appReply[0] = appReply[1] = appReply[2] = 0xEE;

  ebd::runBegin(1000);
  Serial1.print("AT+B;");
  appGot = Serial1.readBytes(appReply, sizeof(appReply));
  char dump[40];
  modem.dump(dump, sizeof(dump));
  ebd::dumpf("%s", dump);
  ebd::runEnd();
  ebd::formatTrace(out, cap);
}

static char run1[1024];
static char run2[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start serial_binary");
  Serial1.begin(9600);
  Serial1.setTimeout(10);
  modem.attach(&draftPort);
  ebd::bindUartDevice(&devUartTx);

  runOnce(run1, sizeof(run1));
  Serial.printf("values got=%u bytes=%02X%02X%02X\n",
                static_cast<unsigned>(appGot), appReply[0], appReply[1],
                appReply[2]);
  Serial.print(run1);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
