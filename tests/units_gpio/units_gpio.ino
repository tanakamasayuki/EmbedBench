// GPIO-shaped units on the host core, driven by ordinary Arduino code: a
// button read through an interrupt, a PIR whose hold time outlives the
// motion, a relay switched by digitalWrite, and an ultrasonic ranger whose
// answer is the width of a pulse.
#include <Arduino.h>
#include <EmbedBench.h>
#include <HostBus.h>
#include <HostInterrupt.h>
#include <embedbench_draft.h>
#include <string.h>

#include <unit_button_model.h>
#include <unit_pir_model.h>
#include <unit_relay_model.h>
#include <unit_sonic_model.h>

static UnitButtonModel button;
static UnitPirModel pir;
static UnitRelayModel relay;
static UnitSonicModel sonic;

// Pin map for this board: each unit's logical line 0 is a real pin.
static const uint8_t kPinButton = 26;
static const uint8_t kPinPir = 25;
static const uint8_t kPinRelay = 19;
static const uint8_t kPinTrigger = 21;
static const uint8_t kPinEcho = 22;

// [adapter begin]
class UnitPort : public ebdev::HostPort {
 public:
  UnitPort(uint8_t outPin, uint8_t channelBase)
      : outPin_(outPin), channelBase_(channelBase) {}
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    ebd::pinInject(ebd::Origin::kDev, outPin_, level);
  }
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool requestWake(uint64_t whenUs) override {
    return ebd::requestWake(whenUs);
  }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
    return true;
  }
  uint8_t channelBase() const { return channelBase_; }

 private:
  uint8_t outPin_;
  uint8_t channelBase_;
};

static UnitPort buttonPort(kPinButton, 0);
static UnitPort pirPort(kPinPir, 1);
static UnitPort relayPort(0xFF, 2);
static UnitPort sonicPort(kPinEcho, 3);

// One channel number per unit, so the director can address them all.
static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  switch (channel) {
    case 0:
      return button.channelWrite(UnitButtonModel::kChannelPress, data, len);
    case 1:
      return pir.channelWrite(UnitPirModel::kChannelMotion, data, len);
    case 3:
      return sonic.channelWrite(UnitSonicModel::kChannelRange, data, len);
    default:
      return false;
  }
}

// The application's pin writes reach the units that listen on a line.
static void routePins(uint8_t pin, uint8_t value, void*) {
  if (pin == kPinRelay) relay.lineIn(UnitRelayModel::kLineControl, value);
  if (pin == kPinTrigger) sonic.lineIn(UnitSonicModel::kLineTrigger, value);
}

static void advanceUnits(uint64_t nowUs, void*) {
  pir.advanceTo(nowUs);
  sonic.advanceTo(nowUs);
}
// [adapter end]

// --- Application ------------------------------------------------------------
static volatile uint32_t buttonEdges = 0;
static void onButton() { ++buttonEdges; }

static uint32_t pirHighAt = 0;
static uint32_t pirLowAt = 0;
static uint32_t echoWidth = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_gpio");
  pinMode(kPinButton, INPUT_PULLUP);
  pinMode(kPinPir, INPUT);
  pinMode(kPinRelay, OUTPUT);
  pinMode(kPinTrigger, OUTPUT);
  pinMode(kPinEcho, INPUT);

  button.attach(&buttonPort);
  pir.attach(&pirPort);
  relay.attach(&relayPort);
  sonic.attach(&sonicPort);
  ebd::setChannelHandler(&routeChannel);
  ebd::setPinWriteForward(&routePins);
  ebd::bindTickDevice(&advanceUnits);

  button.reset();
  pir.reset();
  relay.reset();
  sonic.reset();

  ebd::runBegin(1000);
  attachInterrupt(kPinButton, &onButton, FALLING);

  // A press pulls the line down; the sketch sees the falling edge.
  const uint8_t pressed[1] = {1};
  const uint8_t released[1] = {0};
  ebd::chanWrite(ebd::Origin::kDir, 0, pressed, 1);
  const int buttonLevel = digitalRead(kPinButton);
  ebd::chanWrite(ebd::Origin::kDir, 0, released, 1);

  // Motion raises the PIR line, and it stays up for the unit's hold time
  // after the motion is over.
  const uint8_t motion[1] = {1};
  ebd::chanWrite(ebd::Origin::kDir, 1, motion, 1);
  pirHighAt = digitalRead(kPinPir);
  delay(3);  // longer than the 2500 us hold
  pirLowAt = digitalRead(kPinPir);

  // The relay follows the control line; switching it twice in a row is
  // faster than the contacts settle, and the unit says so.
  digitalWrite(kPinRelay, HIGH);
  digitalWrite(kPinRelay, LOW);

  // A ranging cycle: trigger, then measure how long the echo stays high.
  const uint8_t range[2] = {0x00, 0x64};  // 100 mm -> 600 us of echo
  ebd::chanWrite(ebd::Origin::kDir, 3, range, 2);
  digitalWrite(kPinTrigger, HIGH);
  digitalWrite(kPinTrigger, LOW);
  uint32_t spins = 0;
  while (digitalRead(kPinEcho) == LOW && spins < 100) {
    ++spins;
    delayMicroseconds(100);
  }
  const uint64_t echoStart = ebd::nowUs();
  spins = 0;
  while (digitalRead(kPinEcho) == HIGH && spins < 100) {
    ++spins;
    delayMicroseconds(100);
  }
  echoWidth = static_cast<uint32_t>(ebd::nowUs() - echoStart);

  char text[64];
  button.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  relay.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  sonic.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();

  static char trace[3072];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values level=%d edges=%u pir_high=%d pir_low=%d echo_us=%u\n",
                buttonLevel, buttonEdges, pirHighAt, pirLowAt, echoWidth);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
