// Serial byte-stream contract on the host core: one print() versus five
// single-byte write() calls produce different uart.tx records (the
// environment records what the application did) but the same device
// reply at the same virtual time.
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
  void serialOut(const uint8_t* data, size_t len) override {
    char text[16];
    snprintf(text, sizeof(text), "%.*s", static_cast<int>(len),
             reinterpret_cast<const char*>(data));
    ebd::uartInject(ebd::Origin::kDev, text);
  }
};

static DraftPort draftPort;

static void devUartTx(const uint8_t* data, size_t len, void*) {
  modem.serialIn(data, len);
}
static void advanceDevice(uint64_t nowUs, void*) { modem.advanceTo(nowUs); }
// [adapter end]

static char reply1[3] = {0};
static char reply2[3] = {0};
static uint64_t elapsed1 = 0;
static uint64_t elapsed2 = 0;

static void readReply(char* out, uint64_t* elapsed) {
  const uint64_t before = ebd::nowUs();
  uint8_t buf[2] = {0};
  Serial1.readBytes(buf, sizeof(buf));
  out[0] = static_cast<char>(buf[0]);
  out[1] = static_cast<char>(buf[1]);
  *elapsed = ebd::nowUs() - before;
}

static void runOnce(char* out, size_t cap) {
  modem.reset();
  uint8_t drain[16];
  while (Serial1.readTx(drain, sizeof(drain)) > 0) {
  }
  ebd::runBegin(1000);
  Serial1.print("AT+S;");
  readReply(reply1, &elapsed1);
  const char* text = "AT+S;";
  for (int i = 0; i < 5; ++i) Serial1.write(static_cast<uint8_t>(text[i]));
  readReply(reply2, &elapsed2);
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
  Serial.println("TEST start serial_stream");
  Serial1.begin(9600);
  Serial1.setTimeout(10);
  modem.attach(&draftPort);
  ebd::bindUartDevice(&devUartTx);
  ebd::bindTickDevice(&advanceDevice);

  runOnce(run1, sizeof(run1));
  Serial.printf("values whole=%s e1=%llu split=%s e2=%llu\n", reply1,
                static_cast<unsigned long long>(elapsed1), reply2,
                static_cast<unsigned long long>(elapsed2));
  Serial.print(run1);
  runOnce(run2, sizeof(run2));
  Serial.printf("run2_same=%d\n", strcmp(run1, run2) == 0 ? 1 : 0);
  Serial.println("TEST done");
}

void loop() { delay(10); }
