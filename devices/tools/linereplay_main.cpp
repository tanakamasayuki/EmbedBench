// Play the requests of a recorded trace into one model and print the trace
// the model would have produced. devices/tools/tracecmp.py builds this with
// the model chosen at compile time and compares the two traces:
//
//   g++ -std=c++11 -I<src> -I<devices/src>
//       -DEB_MODEL_HEADER='"qmp6988_model.h"' -DEB_MODEL_CLASS=Qmp6988Model
//       linereplay_main.cpp qmp6988_model.cpp regtable_model.cpp -o replay
//   replay --addr 70 [--channel <n> <hex>]... < recorded.trace
//
// Input and output are event lines (trace2regtable.py's format). Every
// request is made at its recorded time, after the model has been advanced
// through the wakes it asked for, so a difference in the output is a
// difference in behaviour, never in when the question was asked. A
// request to another address is answered as absent (status=2). Model
// diagnostics come out as `<time> mark src=model text=<percent-encoded>`.
// Pure C++11 apart from stdio.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <embedbench_device.h>

#include EB_MODEL_HEADER

namespace {

class ReplayPort : public ebdev::HostPort {
 public:
  uint64_t now = 0;
  uint64_t wakes[32];
  size_t wakeCount = 0;

  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool requestWake(uint64_t whenUs) override {
    if (wakeCount < sizeof(wakes) / sizeof(wakes[0])) wakes[wakeCount++] = whenUs;
    return true;
  }
  bool diagnose(const char* text) override {
    printf("%llu mark src=model text=", static_cast<unsigned long long>(now));
    for (const char* p = text; *p != '\0'; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.') {
        putchar(c);
      } else {
        printf("%%%02X", c);
      }
    }
    putchar('\n');
    return true;
  }

  // Advance the model through every wake due by `t`, in time order, then
  // to `t` itself: what an environment's tick loop does.
  void advance(ebdev::Device& model, uint64_t t) {
    for (;;) {
      size_t best = wakeCount;
      for (size_t i = 0; i < wakeCount; ++i) {
        if (wakes[i] <= t && (best == wakeCount || wakes[i] < wakes[best])) {
          best = i;
        }
      }
      if (best == wakeCount) break;
      const uint64_t when = wakes[best];
      wakes[best] = wakes[--wakeCount];
      if (when > now) now = when;
      model.advanceTo(now);
    }
    now = t;
    model.advanceTo(now);
  }
};

size_t parseHex(const char* text, uint8_t* out, size_t cap) {
  size_t n = 0;
  while (text[0] != '\0' && text[1] != '\0' && n < cap) {
    char pair[3] = {text[0], text[1], 0};
    out[n++] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
    text += 2;
  }
  return n;
}

// The value of `key=` in a line, copied into `out` ("" when absent).
void field(const char* line, const char* key, char* out, size_t cap) {
  out[0] = '\0';
  const size_t klen = strlen(key);
  for (const char* p = line; (p = strstr(p, key)) != nullptr; p += klen) {
    if ((p == line || p[-1] == ' ') && p[klen] == '=') {
      const char* v = p + klen + 1;
      size_t n = strcspn(v, " \n");
      if (n >= cap) n = cap - 1;
      memcpy(out, v, n);
      out[n] = '\0';
      return;
    }
  }
}

void printHex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) printf("%02X", data[i]);
}

}  // namespace

int main(int argc, char** argv) {
  unsigned addr = 0x100;
  EB_MODEL_CLASS model;
  ReplayPort port;
  model.attach(&port);
  model.reset();
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
      addr = static_cast<unsigned>(strtoul(argv[++i], nullptr, 16));
    } else if (strcmp(argv[i], "--channel") == 0 && i + 2 < argc) {
      static uint8_t buf[512];
      const uint8_t channel = static_cast<uint8_t>(atoi(argv[++i]));
      const size_t len = parseHex(argv[++i], buf, sizeof(buf));
      if (!model.channelWrite(channel, buf, len)) {
        fprintf(stderr, "channel %u refused %u bytes\n", channel,
                static_cast<unsigned>(len));
        return 2;
      }
    } else {
      fprintf(stderr, "usage: %s --addr <hex> [--channel <n> <hex>]...\n",
              argv[0]);
      return 2;
    }
  }
  if (addr > 0x7F) {
    fprintf(stderr, "--addr is required\n");
    return 2;
  }

  char line[1024];
  char value[600];
  uint8_t data[300];
  while (fgets(line, sizeof(line), stdin) != nullptr) {
    char* end = strchr(line, '\n');
    if (end != nullptr) *end = '\0';
    char* rest = nullptr;
    const unsigned long long t = strtoull(line, &rest, 10);
    if (rest == line || *rest != ' ') continue;
    ++rest;
    const size_t restLen = strlen(rest);
    const bool rs = restLen >= 3 && strcmp(rest + restLen - 3, " rs") == 0;
    if (strncmp(rest, "mark ", 5) == 0) {
      printf("%s\n", line);
      continue;
    }
    if (strncmp(rest, "i2c.req ", 8) == 0) {
      field(rest, "addr", value, sizeof(value));
      const unsigned a = static_cast<unsigned>(strtoul(value, nullptr, 16));
      field(rest, "data", value, sizeof(value));
      const size_t len = parseHex(value, data, sizeof(data));
      field(rest, "stop", value, sizeof(value));
      const ebdev::I2cTransfer xfer = {value[0] != '0', rs};
      port.advance(model, t);
      const uint8_t status =
          a == addr ? model.i2cWrite(data, len, xfer)
                    : static_cast<uint8_t>(ebdev::kI2cAddressNack);
      printf("%s\n", line);
      printf("%llu i2c.resp status=%u\n", t, status);
      continue;
    }
    if (strncmp(rest, "i2c.rd.req ", 11) == 0) {
      field(rest, "addr", value, sizeof(value));
      const unsigned a = static_cast<unsigned>(strtoul(value, nullptr, 16));
      field(rest, "req", value, sizeof(value));
      size_t req = strtoul(value, nullptr, 10);
      if (req > sizeof(data)) req = sizeof(data);
      field(rest, "stop", value, sizeof(value));
      const ebdev::I2cTransfer xfer = {value[0] != '0', rs};
      port.advance(model, t);
      printf("%s\n", line);
      if (a != addr) {
        printf("%llu i2c.rd.resp len=0 data= status=2\n", t);
      } else {
        const size_t got = model.i2cRead(data, req, xfer);
        printf("%llu i2c.rd.resp len=%u data=", t, static_cast<unsigned>(got));
        printHex(data, got);
        printf("\n");
      }
      continue;
    }
    // Responses are the model's to give; anything else is not replayed.
  }
  char dump[256];
  model.dump(dump, sizeof(dump));
  printf("# dump %s\n", dump);
  return 0;
}
