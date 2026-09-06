// Evidence for the revision-1 additions: each one is measured against the
// workaround it replaces, in the same environment, so the comparison is
// about the interface rather than about two different programs.
//
//   analogOut    a sensor that presents a voltage to an ADC input
//   requestWake  a latency that does not divide by the environment's tick
//   diagnose     a protocol error the device notices and nobody can see
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <embedbench_device.h>

#include "../proposed_port.h"

namespace {

// --- A sensor whose output is a voltage, not a bus reply ------------------
// With analogOut it pushes the value itself. Without it, the device can
// only store the value and hope somebody pulls it out through a channel.
class ThermistorModel : public ebdev::Device {
 public:
  static const uint8_t kChannelTemp = 0;
  static const uint8_t kLineAnalog = 0;

  // The proposal is handed in separately: the frozen interface has no
  // analog path, which is exactly what this experiment measures.
  void usePort(proposal::ProposedPort* port) { proposed_ = port; }

  void reset() override {
    raw_ = 0;
    pushed_ = 0;
    refused_ = 0;
  }

  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override {
    if (channel != kChannelTemp || len != 2) return false;
    raw_ = static_cast<uint16_t>((data[0] << 8) | data[1]);
    if (proposed_ != nullptr) {
      if (proposed_->analogOut(kLineAnalog, raw_)) {
        ++pushed_;
      } else {
        ++refused_;  // no analog routing: the value stays inside the model
      }
    }
    return true;
  }

  // The workaround an environment without analogOut has to use: the
  // director pulls the value out of the model and injects it by hand.
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override {
    if (channel != kChannelTemp) return ebdev::kChannelUnsupported;
    if (cap >= 1) out[0] = static_cast<uint8_t>(raw_ >> 8);
    if (cap >= 2) out[1] = static_cast<uint8_t>(raw_ & 0xFF);
    return 2;
  }

  uint32_t pushed() const { return pushed_; }
  uint32_t refused() const { return refused_; }

 private:
  proposal::ProposedPort* proposed_ = nullptr;
  uint16_t raw_ = 0;
  uint32_t pushed_ = 0;
  uint32_t refused_ = 0;
};

// --- A device whose latency does not divide by the tick -------------------
class LatencyModel : public ebdev::Device {
 public:
  static const uint64_t kLatencyUs = 1500;

  void usePort(proposal::ProposedPort* port) { proposed_ = port; }

  void reset() override {
    due_ = 0;
    pending_ = false;
    repliedAt_ = 0;
    wakeAccepted_ = false;
  }

  void serialIn(const uint8_t*, size_t len) override {
    if (len == 0 || port() == nullptr) return;
    due_ = port()->nowMicros() + kLatencyUs;
    pending_ = true;
    // Ask to be advanced exactly when the reply is due. An environment
    // that cannot schedule says so, and the device is served on the
    // environment's own boundaries instead.
    wakeAccepted_ = proposed_ != nullptr && proposed_->requestWake(due_);
  }

  void advanceTo(uint64_t nowUs) override {
    if (pending_ && nowUs >= due_) {
      pending_ = false;
      repliedAt_ = nowUs;
      const uint8_t ok[2] = {'O', 'K'};
      if (port() != nullptr) port()->serialOut(ok, sizeof(ok));
    }
  }

  uint64_t repliedAt() const { return repliedAt_; }
  bool wakeAccepted() const { return wakeAccepted_; }

 private:
  proposal::ProposedPort* proposed_ = nullptr;
  uint64_t due_ = 0;
  bool pending_ = false;
  uint64_t repliedAt_ = 0;
  bool wakeAccepted_ = false;
};

// --- A device that notices a protocol error it cannot answer with --------
class StrictModel : public ebdev::Device {
 public:
  void usePort(proposal::ProposedPort* port) { proposed_ = port; }

  void reset() override {
    started_ = false;
    errors_ = 0;
    reported_ = 0;
  }

  void serialIn(const uint8_t* data, size_t len) override {
    for (size_t i = 0; i < len; ++i) {
      const char c = static_cast<char>(data[i]);
      if (c == '<') {
        started_ = true;
      } else if (c == '>') {
        started_ = false;
      } else if (!started_) {
        // A byte outside a frame. Serial has no return value to say this
        // with, so without diagnose() it can only be counted internally.
        ++errors_;
        if (proposed_ != nullptr && proposed_->diagnose("byte outside frame")) {
          ++reported_;
        }
      }
    }
  }

  size_t dump(char* out, size_t cap) override {
    const int n = snprintf(out, cap, "strict errors=%u reported=%u", errors_,
                           reported_);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }

  uint32_t errors() const { return errors_; }
  uint32_t reported() const { return reported_; }

 private:
  proposal::ProposedPort* proposed_ = nullptr;
  bool started_ = false;
  uint32_t errors_ = 0;
  uint32_t reported_ = 0;
};

// --- A miniature environment whose capabilities can be switched off ------
struct MiniEnv : public proposal::ProposedPort {
  bool routeAnalog = true;
  bool routeWake = true;
  bool routeDiagnostics = true;

  uint64_t now = 0;
  uint32_t tickUs = 1000;
  uint64_t wakeAt = 0;
  bool wakePending = false;

  uint16_t heldRaw = 0;         // what the application would read
  uint32_t directorSteps = 0;   // work the director had to do by hand
  uint32_t diagnostics = 0;
  char lastDiagnostic[32] = {0};

  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }

  bool analogOut(uint8_t, uint16_t raw) override {
    if (!routeAnalog) return false;
    heldRaw = raw;
    return true;
  }

  bool requestWake(uint64_t whenUs) override {
    if (!routeWake) return false;
    wakeAt = whenUs;
    wakePending = true;
    return true;
  }

  bool diagnose(const char* text) override {
    if (!routeDiagnostics) return false;
    ++diagnostics;
    snprintf(lastDiagnostic, sizeof(lastDiagnostic), "%s", text);
    return true;
  }

  // Advance to `target`, stopping at tick boundaries and, when the
  // environment routes them, at any requested wake time in between.
  void runUntil(uint64_t target, ebdev::Device* device) {
    while (now < target) {
      uint64_t next = ((now / tickUs) + 1) * tickUs;
      if (wakePending && wakeAt > now && wakeAt < next) next = wakeAt;
      if (next > target) next = target;
      now = next;
      if (wakePending && now >= wakeAt) wakePending = false;
      device->advanceTo(now);
    }
  }

  // The workaround for a missing analogOut: pull the value out of the
  // model and inject it, once per update, from outside.
  void pullAnalog(ebdev::Device* device, uint8_t channel) {
    uint8_t buf[2] = {0};
    if (device->channelRead(channel, buf, sizeof(buf)) == 2) {
      heldRaw = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
      ++directorSteps;
    }
  }
};

void measureAnalog() {
  const uint8_t sample[2] = {0x04, 0xD2};  // 1234

  MiniEnv without;
  without.routeAnalog = false;
  ThermistorModel a;
  a.attach(&without);
  a.usePort(&without);
  a.reset();
  a.channelWrite(ThermistorModel::kChannelTemp, sample, 2);
  const uint16_t beforePull = without.heldRaw;
  without.pullAnalog(&a, ThermistorModel::kChannelTemp);

  MiniEnv with;
  ThermistorModel b;
  b.attach(&with);
  b.usePort(&with);
  b.reset();
  b.channelWrite(ThermistorModel::kChannelTemp, sample, 2);

  printf("analog without_routing=%u after_pull=%u director_steps=%u "
         "refused=%u | with_routing=%u director_steps=%u pushed=%u\n",
         beforePull, without.heldRaw, without.directorSteps, a.refused(),
         with.heldRaw, with.directorSteps, b.pushed());
}

void measureWake() {
  MiniEnv without;
  without.routeWake = false;
  LatencyModel a;
  a.attach(&without);
  a.usePort(&without);
  a.reset();
  a.serialIn(reinterpret_cast<const uint8_t*>("go"), 2);
  without.runUntil(4000, &a);

  MiniEnv with;
  LatencyModel b;
  b.attach(&with);
  b.usePort(&with);
  b.reset();
  b.serialIn(reinterpret_cast<const uint8_t*>("go"), 2);
  with.runUntil(4000, &b);

  printf("wake due=%llu without_routing_at=%llu accepted=%d | "
         "with_routing_at=%llu accepted=%d\n",
         static_cast<unsigned long long>(LatencyModel::kLatencyUs),
         static_cast<unsigned long long>(a.repliedAt()),
         a.wakeAccepted() ? 1 : 0,
         static_cast<unsigned long long>(b.repliedAt()),
         b.wakeAccepted() ? 1 : 0);
}

void measureDiagnose() {
  MiniEnv without;
  without.routeDiagnostics = false;
  StrictModel a;
  a.attach(&without);
  a.usePort(&without);
  a.reset();
  a.serialIn(reinterpret_cast<const uint8_t*>("x<ok>"), 5);

  MiniEnv with;
  StrictModel b;
  b.attach(&with);
  b.usePort(&with);
  b.reset();
  b.serialIn(reinterpret_cast<const uint8_t*>("x<ok>"), 5);

  printf("diagnose without_routing errors=%u reported=%u recorded=%u | "
         "with_routing errors=%u reported=%u recorded=%u last=%s\n",
         a.errors(), a.reported(), without.diagnostics, b.errors(),
         b.reported(), with.diagnostics, with.lastDiagnostic);
}

}  // namespace

int main() {
  printf("NATIVE start\n");
  printf("revision version=%u revision=%03u\n",
         ebdev::kDeviceInterfaceVersion, ebdev::kDeviceInterfaceRevision);
  measureAnalog();
  measureWake();
  measureDiagnose();
  printf("NATIVE done\n");
  return 0;
}
