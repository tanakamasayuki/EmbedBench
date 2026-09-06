// The radio-shaped units, all on the generic frame path: an IR remote
// whose held button repeats, a LoRa modem whose air time grows with the
// payload, and three UWB anchors answering one broadcast poll in slot
// order. Three logical links (bus 0, 1, 2) are live at once.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_draft.h>
#include <string.h>

#include <unit_ir_model.h>
#include <unit_lora_model.h>
#include <unit_uwb_model.h>

static UnitIrModel ir;
static UnitLoraModel lora;
static UnitUwbModel anchor0(0);
static UnitUwbModel anchor1(1);
static UnitUwbModel anchor2(2);

static const uint8_t kPinTxDone = 12;

// [adapter begin]
class RadioPort : public ebdev::HostPort {
 public:
  explicit RadioPort(uint8_t donePin = 0xFF) : donePin_(donePin) {}
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    if (donePin_ != 0xFF) ebd::pinInject(ebd::Origin::kDev, donePin_, level);
  }
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebd::frameRx(ebd::Origin::kDev, bus, format, data, bits);
  }
  uint16_t formatId(const char* name, uint32_t schema) override {
    return ebd::registerFormat(name, schema);
  }
  uint32_t maxFrameBits(uint8_t) override { return ebd::frameCapacityBits(); }
  bool requestWake(uint64_t whenUs) override { return ebd::requestWake(whenUs); }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
    return true;
  }

 private:
  uint8_t donePin_;
};

static RadioPort irPort;
static RadioPort loraPort(kPinTxDone);
static RadioPort uwbPort;

// One frame handler for every link: the bus id is what says which radio
// hears the frame. Bus 2 is a broadcast — all three anchors get it.
static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  switch (bus) {
    case UnitIrModel::kBus:
      ir.frameIn(bus, format, data, bits);
      break;
    case UnitLoraModel::kBus:
      lora.frameIn(bus, format, data, bits);
      break;
    case UnitUwbModel::kBus:
      anchor0.frameIn(bus, format, data, bits);
      anchor1.frameIn(bus, format, data, bits);
      anchor2.frameIn(bus, format, data, bits);
      break;
    default:
      break;
  }
}

static void advanceRadios(uint64_t nowUs, void*) {
  ir.advanceTo(nowUs);
  lora.advanceTo(nowUs);
  anchor0.advanceTo(nowUs);
  anchor1.advanceTo(nowUs);
  anchor2.advanceTo(nowUs);
}

static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  switch (channel) {
    case 0: return ir.channelWrite(UnitIrModel::kChannelPress, data, len);
    case 1: return lora.channelWrite(UnitLoraModel::kChannelDownlink, data, len);
    case 2: return anchor0.channelWrite(UnitUwbModel::kChannelDistance, data, len);
    case 3: return anchor1.channelWrite(UnitUwbModel::kChannelDistance, data, len);
    case 4: return anchor2.channelWrite(UnitUwbModel::kChannelDistance, data, len);
    default: return false;
  }
}
// [adapter end]

// --- Application-side shims -------------------------------------------------
static uint16_t irCode = 0;
static uint16_t irRepeat = 0;
static uint16_t loraUp = 0;
static uint16_t loraDown = 0;
static uint16_t uwbPoll = 0;
static uint16_t uwbResp = 0;

// What the application actually cares about.
static uint8_t appPresses = 0;   // one press, however many repeats arrive
static uint8_t appRepeats = 0;
static uint8_t appLastCmd = 0;
static uint8_t appDownlink[8] = {0};
static size_t appDownlinkLen = 0;
static uint16_t appRanges[3] = {0, 0, 0};
static uint8_t appRangeOrder[3] = {0xFF, 0xFF, 0xFF};
static uint8_t appRangeCount = 0;

static void appFrameReceiver(uint8_t bus, uint16_t format, const uint8_t* data,
                             size_t bits, void*) {
  if (bus == UnitIrModel::kBus && format == irCode && bits == 16) {
    // A new code is a new press; the repeats that follow are the same one.
    ++appPresses;
    appLastCmd = data[1];
  } else if (bus == UnitIrModel::kBus && format == irRepeat && bits == 0) {
    ++appRepeats;
  } else if (bus == UnitLoraModel::kBus && format == loraDown) {
    appDownlinkLen = ebdev::frameBytes(bits);
    if (appDownlinkLen > sizeof(appDownlink)) appDownlinkLen = sizeof(appDownlink);
    memcpy(appDownlink, data, appDownlinkLen);
  } else if (bus == UnitUwbModel::kBus && format == uwbResp && bits == 24) {
    const uint8_t id = data[0];
    if (id < 3) appRanges[id] = static_cast<uint16_t>((data[1] << 8) | data[2]);
    if (appRangeCount < 3) appRangeOrder[appRangeCount++] = id;
  }
}

// The 12-bit poll: tag in bits 0-3, sequence in bits 4-11, MSB-first,
// with the four unused low bits of the last byte cleared.
static void appPoll(uint8_t tag, uint8_t seq) {
  const uint8_t frame[2] = {static_cast<uint8_t>((tag << 4) | (seq >> 4)),
                            static_cast<uint8_t>((seq << 4) & 0xF0)};
  ebd::frameTx(ebd::Origin::kApp, UnitUwbModel::kBus, uwbPoll, frame, 12);
}

static bool appLoraSend(const uint8_t* payload, size_t len) {
  return ebd::frameTx(ebd::Origin::kApp, UnitLoraModel::kBus, loraUp, payload,
                      len * 8);
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start units_radio");
  pinMode(kPinTxDone, INPUT);

  ir.attach(&irPort);
  lora.attach(&loraPort);
  anchor0.attach(&uwbPort);
  anchor1.attach(&uwbPort);
  anchor2.attach(&uwbPort);
  ebd::bindFrameDevice(&devFrame);
  ebd::setFrameReceiver(&appFrameReceiver);
  ebd::setChannelHandler(&routeChannel);
  ebd::bindTickDevice(&advanceRadios);
  ir.reset();
  lora.reset();
  anchor0.reset();
  anchor1.reset();
  anchor2.reset();

  // The application resolves the same names the devices do; the ids come
  // from the environment, so both sides agree without sharing a number.
  irCode = ebd::registerFormat("m5.ir.nec.1", ebdev::schemaFingerprint("u8 addr,u8 cmd"));
  irRepeat = ebd::registerFormat("m5.ir.rep.1", ebdev::schemaFingerprint("empty"));
  loraUp = ebd::registerFormat("m5.lora.up.1", ebdev::schemaFingerprint("u8 payload[]"));
  loraDown = ebd::registerFormat("m5.lora.dn.1", ebdev::schemaFingerprint("u8 payload[]"));
  uwbPoll = ebd::registerFormat("m5.uwb.poll.1", ebdev::schemaFingerprint("u4 tag,u8 seq"));
  uwbResp = ebd::registerFormat("m5.uwb.resp.1", ebdev::schemaFingerprint("u8 anchor,u16 mm"));

  ebd::runBegin(1000);

  // IR: the volume-up button is held. One code frame, then two repeats,
  // each an empty frame carrying no payload at all.
  const uint8_t press[3] = {0x40, 0x12, 0x02};
  ebd::chanWrite(ebd::Origin::kDir, 0, press, 3);
  delay(5);
  // The unit also receives: another remote in the room sends a code.
  const uint8_t foreign[2] = {0x40, 0x99};
  ebd::frameTx(ebd::Origin::kApp, UnitIrModel::kBus, irCode, foreign, 16);
  uint8_t lastRx[2] = {0, 0};
  ir.channelRead(UnitIrModel::kChannelLastRx, lastRx, sizeof(lastRx));

  // LoRa: a 4-byte uplink takes 4,000 us of air, and a send during that
  // window is lost, not queued.
  const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  const bool firstAccepted = appLoraSend(payload, sizeof(payload));
  // The transport takes it and the modem drops it: the application is
  // told by neither return value, only by the TxDone line being low.
  const int busyLevel = digitalRead(kPinTxDone);
  const bool secondAccepted = appLoraSend(payload, 1);  // still transmitting
  delay(5);
  const int doneLevel = digitalRead(kPinTxDone);
  // Two different limits. Six bytes fit the transport but not this
  // modem; twelve exceed the transport itself and never reach the modem.
  uint8_t big[12];
  memset(big, 0x5A, sizeof(big));
  const bool sixAccepted = appLoraSend(big, 6);
  const bool twelveAccepted = appLoraSend(big, sizeof(big));
  // A downlink arrives with a signal quality that is not in the payload.
  const uint8_t downlink[5] = {97, 8, 0x01, 0x02, 0x03};  // -97 dBm, 8 dB
  ebd::chanWrite(ebd::Origin::kDir, 1, downlink, 5);
  uint8_t status[3] = {0, 0, 0};
  const size_t statusLen = lora.channelRead(UnitLoraModel::kChannelStatus,
                                            status, sizeof(status));

  // UWB: three anchors at different distances answer one broadcast poll,
  // each in its own slot.
  const uint8_t d0[2] = {0x00, 0xC8};  // 200 mm
  const uint8_t d1[2] = {0x03, 0xE8};  // 1,000 mm
  const uint8_t d2[2] = {0x0B, 0xB8};  // 3,000 mm
  ebd::chanWrite(ebd::Origin::kDir, 2, d0, 2);
  ebd::chanWrite(ebd::Origin::kDir, 3, d1, 2);
  ebd::chanWrite(ebd::Origin::kDir, 4, d2, 2);
  appPoll(0x5, 0xA3);
  appPoll(0x5, 0xA4);  // too soon: the round is still open
  delay(3);

  char text[64];
  ir.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  lora.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  anchor2.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();

  static char trace[4096];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values presses=%u repeats=%u cmd=%02X rx=%02X%02X\n",
                appPresses, appRepeats, appLastCmd, lastRx[0], lastRx[1]);
  Serial.printf("values tx1=%d tx2=%d six=%d twelve=%d busy=%d done=%d\n",
                firstAccepted ? 1 : 0, secondAccepted ? 1 : 0,
                sixAccepted ? 1 : 0, twelveAccepted ? 1 : 0, busyLevel,
                doneLevel);
  Serial.printf("values dn_len=%u dn=%02X%02X%02X rssi=-%u snr=%u st=%u\n",
                static_cast<unsigned>(appDownlinkLen), appDownlink[0],
                appDownlink[1], appDownlink[2], status[0], status[1],
                static_cast<unsigned>(statusLen));
  Serial.printf("values ranges=%u,%u,%u order=%u%u%u n=%u\n", appRanges[0],
                appRanges[1], appRanges[2], appRangeOrder[0], appRangeOrder[1],
                appRangeOrder[2], appRangeCount);
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n", s.events, s.dropped,
                s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
