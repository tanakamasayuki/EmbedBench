// Native comparison of format identification schemes: fixed numbers
// (silent collision), environment-interned names (this project's
// candidate), per-frame strings (cost baseline), and the degradation when
// no registry exists. Plain g++, no Arduino.
#include <stdio.h>
#include <string.h>

#include "../named_node_model.h"

namespace {

uint32_t strcmpCalls = 0;
int countingStrcmp(const char* a, const char* b) {
  ++strcmpCalls;
  return strcmp(a, b);
}

// Interning port: a small name registry, ids assigned in first-come order.
struct RegistryPort : public ebdev::HostPort {
  uint64_t now = 0;
  bool used[4] = {false, false, false, false};
  char names[4][20];
  uint8_t lastBus = 0xFF;
  uint16_t lastFormat = 0;
  uint8_t lastFrame[4] = {0};
  uint32_t frames = 0;

  uint64_t nowMicros() override { return now; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,
                size_t bits) override {
    lastBus = bus;
    lastFormat = format;
    const size_t bytes = (bits + 7) / 8;
    memcpy(lastFrame, data,
           bytes < sizeof(lastFrame) ? bytes : sizeof(lastFrame));
    ++frames;
    return true;
  }
  uint16_t formatId(const char* name, uint32_t) override {
    for (size_t i = 0; i < 4; ++i) {
      if (used[i] && countingStrcmp(names[i], name) == 0) {
        return static_cast<uint16_t>(i + 1);
      }
    }
    for (size_t i = 0; i < 4; ++i) {
      if (!used[i]) {
        used[i] = true;
        snprintf(names[i], sizeof(names[i]), "%s", name);
        return static_cast<uint16_t>(i + 1);
      }
    }
    return 0;
  }
};

// Baseline: a model that hard-codes format number 1. Two independent
// libraries picking the same number cannot be detected.
struct NumericNode {
  uint8_t power = 1;
  void frameIn(uint16_t format, const uint8_t* data, size_t) {
    if (format == 1 && data[0] == 0x04) {
      power = data[1] == 0x08 ? 1 : 0;
    }
  }
};

// String-only variant: no registry, but every frame pays a strcmp.
struct StringNode {
  uint8_t power = 0;
  void frameIn(const char* format, const uint8_t* data, size_t) {
    if (countingStrcmp(format, "acme.node.1") == 0 && data[0] == 0x04) {
      power = data[1] == 0x08 ? 1 : 0;
    }
  }
};

// An environment without frame routing: default formatId returns 0.
struct PlainPort : public ebdev::HostPort {
  uint32_t frames = 0;
  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
  bool frameOut(uint8_t, uint16_t, const uint8_t*, size_t) override {
    ++frames;
    return true;
  }
};

}  // namespace

int main() {
  printf("NATIVE start\n");

  // 1) Fixed numbers: a vendor's calibration frame {offset=4, gain=0}
  // also uses "format 1" and silently switches the node off.
  NumericNode numeric;
  const uint8_t vendorCal[2] = {0x04, 0x00};
  numeric.frameIn(1, vendorCal, 16);
  printf("numeric power_before=1 power_after=%u\n", numeric.power);

  // 2) Interned names: the vendor gets its own id, the node its own; the
  // same payload no longer reaches the wrong interpreter.
  RegistryPort port;
  NamedNodeModel node;
  node.attach(&port);
  node.reset();
  const uint16_t vendorId = port.formatId("vend.cal.1", 0x0002);
  node.frameIn(0, vendorId, vendorCal, 16);
  node.advanceTo(1000);
  const uint32_t framesAfterVendor = port.frames;
  const uint16_t cmdId = port.formatId("acme.node.1", 0x0001);
  const uint8_t own[2] = {0x04, 0x08};
  port.now = 0;
  node.frameIn(0, cmdId, own, 16);
  node.advanceTo(1000);
  char nodeDump[40];
  node.dump(nodeDump, sizeof(nodeDump));
  printf("interned vendor_id=%u cmd_id=%u again=%u after_vendor=%u frames=%u "
         "fmt=%u dump=<%s>\n",
         vendorId, cmdId, port.formatId("acme.node.1", 0x0001), framesAfterVendor,
         port.frames, port.lastFormat, nodeDump);

  // Cost: resolved ids compare integers; strings pay one strcmp per frame.
  const uint32_t beforeInterned = strcmpCalls;
  for (int i = 0; i < 100; ++i) node.frameIn(0, cmdId, own, 16);
  const uint32_t internedCost = strcmpCalls - beforeInterned;
  StringNode stringNode;
  const uint32_t beforeStrings = strcmpCalls;
  for (int i = 0; i < 100; ++i) stringNode.frameIn("acme.node.1", own, 16);
  printf("cost interned_100=%u strings_100=%u\n", internedCost,
         strcmpCalls - beforeStrings);

  // 3) No registry: formatId=0, the device matches nothing and stays inert.
  PlainPort plain;
  NamedNodeModel inert;
  inert.attach(&plain);
  inert.reset();
  inert.frameIn(0, 1, own, 16);
  inert.advanceTo(2000);
  char inertDump[40];
  inert.dump(inertDump, sizeof(inertDump));
  printf("noreg frames=%u dump=<%s>\n", plain.frames, inertDump);

  printf("NATIVE done\n");
  return 0;
}
