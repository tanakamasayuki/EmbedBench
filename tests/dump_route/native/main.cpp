// X12's last open question: when an inspection has to become evidence,
// which route should it take? Three candidates on the same device and the
// same check, so the comparison is about the route.
//
//   text     the director formats the device's dump() text into one event
//   channel  the director reads a channel and records the raw fields
//   direct   the director calls the model's own typed API and records
//            whatever it likes (the route the interface cannot see)
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <embedbench_device.h>

namespace {

// A sensor with two facts worth inspecting: a raw sample and a fault flag.
class SensorModel : public ebdev::Device {
 public:
  static const uint8_t kChannelState = 0;

  void reset() override {
    raw_ = 0;
    faulted_ = false;
  }

  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override {
    if (channel != kChannelState || len != 3) return false;
    raw_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
    faulted_ = data[2] != 0;
    return true;
  }

  // Route 2: the fields as bytes, in a shape a test can compare.
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override {
    if (channel != kChannelState) return ebdev::kChannelUnsupported;
    if (cap >= 1) out[0] = static_cast<uint8_t>(raw_ >> 8);
    if (cap >= 2) out[1] = static_cast<uint8_t>(raw_ & 0xFF);
    if (cap >= 3) out[2] = faulted_ ? 1 : 0;
    return 3;
  }

  // Route 1: one line of text meant for a person.
  size_t dump(char* out, size_t cap) override {
    const int n = snprintf(out, cap, "sensor raw=%u fault=%u", raw_,
                           faulted_ ? 1 : 0);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }

  // Route 3: the model's own typed accessors. Nothing about them is
  // visible to the environment, which is exactly the point.
  uint16_t raw() const { return raw_; }
  bool faulted() const { return faulted_; }

 private:
  uint16_t raw_ = 0;
  bool faulted_ = false;
};

struct NullPort : public ebdev::HostPort {
  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
};

// A trace just big enough to compare what each route leaves behind.
struct Trace {
  char lines[4][64];
  size_t count = 0;
  void add(const char* text) {
    if (count < 4) snprintf(lines[count++], sizeof(lines[0]), "%s", text);
  }
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  NullPort port;
  SensorModel sensor;
  sensor.attach(&port);
  sensor.reset();
  const uint8_t state[3] = {0x04, 0xD2, 0x01};  // raw 1234, faulted
  sensor.channelWrite(SensorModel::kChannelState, state, 3);

  // --- Route 1: text dump ------------------------------------------------
  Trace textTrace;
  char text[48];
  sensor.dump(text, sizeof(text));
  char line[64];
  snprintf(line, sizeof(line), "dump %s", text);
  textTrace.add(line);

  // --- Route 2: channel read ---------------------------------------------
  Trace channelTrace;
  uint8_t fields[3] = {0};
  const size_t needed = sensor.channelRead(SensorModel::kChannelState, fields,
                                           sizeof(fields));
  snprintf(line, sizeof(line), "dump.chan chan=0 len=%u data=%02X%02X%02X",
           static_cast<unsigned>(needed), fields[0], fields[1], fields[2]);
  channelTrace.add(line);

  // --- Route 3: the model's own API --------------------------------------
  // The director can record anything it likes, in any shape, and nothing
  // makes the two agree — including the case where it records nothing.
  Trace directTrace;
  snprintf(line, sizeof(line), "note raw=%u", sensor.raw());
  directTrace.add(line);

  // What a test can actually assert on, per route: whether the value can
  // be recovered by a machine without parsing prose, and whether the
  // route is available to the environment at all.
  const bool textMachineReadable = false;  // "sensor raw=1234 fault=1"
  const bool channelMachineReadable = true;
  const uint16_t channelValue =
      static_cast<uint16_t>((fields[0] << 8) | fields[1]);

  printf("route text lines=%zu text=<%s> machine_readable=%d\n",
         textTrace.count, textTrace.lines[0], textMachineReadable ? 1 : 0);
  printf("route channel lines=%zu text=<%s> machine_readable=%d value=%u "
         "fault=%u\n",
         channelTrace.count, channelTrace.lines[0],
         channelMachineReadable ? 1 : 0, channelValue, fields[2]);
  printf("route direct lines=%zu text=<%s> environment_can_see=%d\n",
         directTrace.count, directTrace.lines[0], 0);

  // The same inspection through the interface costs the same either way:
  // one call, one event. The difference is what the event carries.
  printf("cost text_calls=1 channel_calls=1 direct_calls=1 "
         "text_fields=0 channel_fields=3\n");
  printf("NATIVE done\n");
  return 0;
}
