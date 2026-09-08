// The application sequences of X63, written once so the same sequence
// can be driven against the hand-written catalog model (to capture) and
// against the table the capture produced (to replay). A Session is one
// run on environment example #2 with every result the application saw
// kept aside: the trace shows at most five payload bytes, the results
// line shows them all, so the comparison is on what the application got,
// not on what the log had room for.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <embedbench_device.h>
#include <nenv.h>

struct Session {
  nenv::Env env;
  char results[1024];
  size_t pos = 0;

  void begin(ebdev::Device& dev, uint8_t address);
  uint8_t write(uint8_t address, const uint8_t* data, size_t len,
                bool stop = true);
  size_t read(uint8_t address, uint8_t* out, size_t len, bool stop = true);
  void chan(uint8_t channel, const uint8_t* data, size_t len);
  void wait(uint32_t us);
  void end(ebdev::Device& dev);
  void print(const char* variant, const char* name);

 private:
  void append(const char* fmt, ...);
};

typedef void (*Scenario)(Session&, ebdev::Device&);

// tests/native_env: configure, world sets a temperature, read it back.
void scenarioTemp(Session& s, ebdev::Device& dev);
// tests/catalog_devices: identify, forced measurement, poll status early
// and late, read the result, touch an unmapped register.
void scenarioEnv(Session& s, ebdev::Device& dev);
// tests/units_sense: start sampling, come back at the watermark, drain the
// FIFO, be late enough to overflow it, stop.
void scenarioImu(Session& s, ebdev::Device& dev);
