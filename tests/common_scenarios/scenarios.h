// Application sequences shared by the capture experiments (X63, X64):
// written once so the same sequence can be driven against a hand-written
// catalog model (to capture) and against what the capture produced (to
// replay). A Session is one run on environment example #2 with every
// result the application saw kept aside: the trace shows at most five
// payload bytes, the results line shows them all, so the comparison is on
// what the application got, not on what the log had room for.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <embedbench_device.h>
#include <nenv.h>

struct Session {
  nenv::Env env;
  char results[1024];
  size_t pos = 0;

  // The first device: I2C at `address`, or the serial/SPI one when 0. It
  // also gets the world channel and the tick.
  void begin(ebdev::Device& dev, uint8_t address);
  // Another device on the same session, bound the same way (no channel).
  void add(ebdev::Device& dev, uint8_t address);
  uint8_t write(uint8_t address, const uint8_t* data, size_t len,
                bool stop = true);
  size_t read(uint8_t address, uint8_t* out, size_t len, bool stop = true);
  void chan(uint8_t channel, const uint8_t* data, size_t len);
  void serialWrite(const char* text);
  size_t serialRead(uint8_t* out, size_t len, uint32_t timeoutUs);
  uint8_t spi(uint8_t mosi);
  void line(uint8_t line, uint8_t level);
  void wait(uint32_t us);
  void end(ebdev::Device& dev);
  void print(const char* variant, const char* name);

 private:
  void append(const char* fmt, ...);
};

typedef void (*Scenario)(Session&, ebdev::Device&);
typedef void (*Scenario2)(Session&, ebdev::Device&, ebdev::Device&);

// tests/native_env: configure, world sets a temperature, read it back.
void scenarioTemp(Session& s, ebdev::Device& dev);
// tests/catalog_devices: identify, forced measurement, poll status early
// and late, read the result, touch an unmapped register.
void scenarioEnv(Session& s, ebdev::Device& dev);
// tests/units_sense: start sampling, come back at the watermark, drain the
// FIFO, be late enough to overflow it, stop.
void scenarioImu(Session& s, ebdev::Device& dev);
// tests/native_env: two AT commands to the modem, each answered a tick
// later; the second one is unknown to it.
void scenarioModem(Session& s, ebdev::Device& dev);
// The env sequence with the application off the recorded path: it reads
// the result before polling status, and writes a different command.
void scenarioEnvStrayed(Session& s, ebdev::Device& dev);
// Two I2C parts on one bus, the thermometer at 0x48 and the environmental
// sensor at 0x76, addressed in turns.
void scenarioTwoDevices(Session& s, ebdev::Device& temp, ebdev::Device& env);
// tests/units_mixed: the SPI flash — status, write enable, a page program
// that takes its time, and reading it back. Chip select is line 0.
void scenarioFlash(Session& s, ebdev::Device& flash);
// One bus and one serial port at once: the sensor and the modem.
void scenarioEnvAndModem(Session& s, ebdev::Device& env, ebdev::Device& modem);
