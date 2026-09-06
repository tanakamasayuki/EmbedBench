// Interface revisions 002-004 on the host core: the device presents a
// voltage the application reads with analogRead, answers a command 1500 us
// later (which is not a tick boundary), and reports an unknown command it
// cannot refuse any other way.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostUart.h>
#include <embedbench_draft.h>
#include <string.h>

#include "rev1_model.h"

static Rev1Model model;

// [adapter begin]
class DraftPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t* data, size_t len) override {
    return ebd::uartInject(ebd::Origin::kDev, data, len);
  }
  // Analog line 0 of this device is wired to pin 8 here.
  bool analogOut(uint8_t line, uint16_t raw) override {
    if (line != Rev1Model::kLineAnalog) return false;
    ebd::analogInject(ebd::Origin::kDev, 8, raw);
    return true;
  }
  bool requestWake(uint64_t whenUs) override {
    return ebd::requestWake(whenUs);
  }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
    return true;
  }
};

static DraftPort draftPort;

static void devUartTx(const uint8_t* data, size_t len, void*) {
  model.serialIn(data, len);
}
static bool devChannel(uint8_t channel, const uint8_t* data, size_t len,
                       void*) {
  return model.channelWrite(channel, data, len);
}
static void advanceDevice(uint64_t nowUs, void*) { model.advanceTo(nowUs); }
// [adapter end]

static uint16_t appRaw = 0;
static uint64_t appElapsed = 0;
static char appReply[3] = {0};

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start if_rev1");
  Serial1.begin(9600);
  Serial1.setTimeout(10);
  model.attach(&draftPort);
  ebd::bindUartDevice(&devUartTx);
  ebd::setChannelHandler(&devChannel);
  ebd::bindTickDevice(&advanceDevice);
  model.reset();

  ebd::runBegin(1000);

  const uint8_t sample[2] = {0x04, 0xD2};  // 1234
  ebd::chanWrite(ebd::Origin::kDir, Rev1Model::kChannelTemp, sample, 2);
  appRaw = analogRead(8);

  Serial1.write('?');  // refused through the diagnostic path

  const uint64_t before = ebd::nowUs();
  Serial1.write('g');
  uint8_t reply[2] = {0};
  Serial1.readBytes(reply, sizeof(reply));
  appReply[0] = static_cast<char>(reply[0]);
  appReply[1] = static_cast<char>(reply[1]);
  appElapsed = ebd::nowUs() - before;

  char text[48];
  model.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();

  static char trace[1600];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values raw=%u reply=%s elapsed=%llu\n", appRaw, appReply,
                static_cast<unsigned long long>(appElapsed));
  Serial.print(trace);
  Serial.println("TEST done");
}

void loop() { delay(10); }
