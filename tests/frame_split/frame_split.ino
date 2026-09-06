// Per-bus frame capacity, and where the rule for splitting a message
// belongs. Three links carry different amounts per frame; the same
// twenty-byte message becomes a different number of chunks on each, and
// on the link that cannot even hold the header it becomes none at all.
#include <Arduino.h>
#include <EmbedBench.h>
#include <embedbench_draft.h>
#include <string.h>

#include <unit_chunk_model.h>

static UnitChunkModel sender;
static UnitChunkModel receiver;

// The links, narrowest first. All three sit under the environment's own
// per-call capacity, so what refuses a frame here is the link, not the
// transport.
static const uint8_t kBusNarrow = 0;  // 16 bits: header only, no room left
static const uint8_t kBusSmall = 1;   // 32 bits: two payload bytes a frame
static const uint8_t kBusWide = 2;    // 64 bits: six payload bytes a frame

// [adapter begin]
static uint32_t busCapacityBits(uint8_t bus) {
  switch (bus) {
    case kBusNarrow: return 16;
    case kBusSmall: return 32;
    case kBusWide: return 64;
    default: return 0;
  }
}

class SplitPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    return ebd::frameRx(ebd::Origin::kDev, bus, format, data, bits);
  }
  uint16_t formatId(const char* name, uint32_t schema) override {
    return ebd::registerFormat(name, schema);
  }
  // The link's capacity, not the environment's: the interface asks per
  // bus precisely so these can differ.
  uint32_t maxFrameBits(uint8_t bus) override { return busCapacityBits(bus); }
  bool requestWake(uint64_t whenUs) override { return ebd::requestWake(whenUs); }
  bool diagnose(const char* text) override {
    ebd::deviceNote(text);
    return true;
  }
};

static SplitPort senderPort;
static SplitPort receiverPort;

static void devFrame(uint8_t bus, uint16_t format, const uint8_t* data,
                     size_t bits, void*) {
  sender.frameIn(bus, format, data, bits);
}
static bool routeChannel(uint8_t channel, const uint8_t* data, size_t len,
                         void*) {
  if (channel == 0) return sender.channelWrite(UnitChunkModel::kChannelSend,
                                               data, len);
  return false;
}
static void advanceSender(uint64_t nowUs, void*) { sender.advanceTo(nowUs); }
// [adapter end]

// The application side: it hears the chunks the sender put on the link
// and hands them to the far end, which is the same model reassembling.
static uint32_t appFrames[3] = {0, 0, 0};

static void appFrameReceiver(uint8_t bus, uint16_t format, const uint8_t* data,
                             size_t bits, void*) {
  if (bus < 3) ++appFrames[bus];
  receiver.frameIn(bus, format, data, bits);
}

// chanWrite reports through the trace rather than a return value, so
// what the send achieved is read from the frames that appeared on the
// link and from the model's own counters.
static void sendOn(uint8_t bus, const uint8_t* body, size_t len) {
  uint8_t request[1 + UnitChunkModel::kMaxMessage];
  request[0] = bus;
  memcpy(request + 1, body, len);
  ebd::chanWrite(ebd::Origin::kDir, 0, request, len + 1);
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start frame_split");

  sender.attach(&senderPort);
  receiver.attach(&receiverPort);
  ebd::bindFrameDevice(&devFrame);
  ebd::bindTickDevice(&advanceSender);
  ebd::setFrameReceiver(&appFrameReceiver);
  ebd::setChannelHandler(&routeChannel);
  sender.reset();
  receiver.reset();

  ebd::runBegin(1000);

  uint8_t body[20];
  for (size_t i = 0; i < sizeof(body); ++i) {
    body[i] = static_cast<uint8_t>(0xA0 + i);
  }

  // The same message on each link. Twenty bytes with a two-byte header:
  // six per frame on the wide link, two on the small one, and the narrow
  // link cannot carry a chunk at all.
  sendOn(kBusWide, body, sizeof(body));
  delay(3);  // 4 chunks, one every 500 us
  uint8_t got[UnitChunkModel::kMaxMessage] = {0};
  const size_t wideLen = receiver.channelRead(
      UnitChunkModel::kChannelAssembled, got, sizeof(got));
  const bool wideSame = wideLen == sizeof(body) &&
                        memcmp(got, body, sizeof(body)) == 0;

  sendOn(kBusSmall, body, sizeof(body));
  delay(6);  // 10 chunks
  const size_t smallLen = receiver.channelRead(
      UnitChunkModel::kChannelAssembled, got, sizeof(got));
  const bool smallSame = smallLen == sizeof(body) &&
                         memcmp(got, body, sizeof(body)) == 0;

  sendOn(kBusNarrow, body, sizeof(body));

  char text[80];
  sender.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  receiver.dump(text, sizeof(text));
  ebd::dumpf("%s", text);
  ebd::runEnd();

  static char trace[4096];
  ebd::formatTrace(trace, sizeof(trace));
  Serial.printf("values wide=%u,%d small=%u,%d narrow=%u\n",
                appFrames[kBusWide], wideSame ? 1 : 0, appFrames[kBusSmall],
                smallSame ? 1 : 0, appFrames[kBusNarrow]);
  Serial.printf("values caps=%u,%u,%u\n", busCapacityBits(kBusNarrow),
                busCapacityBits(kBusSmall), busCapacityBits(kBusWide));
  Serial.print(trace);
  const ebd::Stats s = ebd::stats();
  Serial.printf("stats events=%u dropped=%u folded=%u diag=%u\n", s.events,
                s.dropped, s.folded, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
