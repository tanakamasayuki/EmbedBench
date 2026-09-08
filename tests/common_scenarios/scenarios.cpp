// Shared application sequences, driven against any Device.
#include "scenarios.h"

#include <stdarg.h>
#include <stdio.h>

void Session::append(const char* fmt, ...) {
  if (pos >= sizeof(results)) return;
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(results + pos, sizeof(results) - pos, fmt, ap);
  va_end(ap);
  if (n > 0) pos += static_cast<size_t>(n);
  if (pos > sizeof(results) - 1) pos = sizeof(results) - 1;
}

void Session::begin(ebdev::Device& dev, uint8_t address) {
  pos = 0;
  results[0] = '\0';
  env.reset();
  add(dev, address);
  env.bindChannel(&dev);
}

void Session::add(ebdev::Device& dev, uint8_t address) {
  dev.attach(&env);
  dev.reset();
  if (address != 0) {
    env.bindI2c(address, &dev);
  } else {
    env.bindSerial(&dev);
    env.bindSpi(&dev);
  }
  env.addTicking(&dev);
}

uint8_t Session::write(uint8_t address, const uint8_t* data, size_t len,
                       bool stop) {
  const uint8_t status = env.i2cWrite(address, data, len, stop);
  append(" W%u", status);
  return status;
}

size_t Session::read(uint8_t address, uint8_t* out, size_t len, bool stop) {
  const size_t got = env.i2cRead(address, out, len, stop);
  append(" R");
  for (size_t i = 0; i < got; ++i) append("%02X", out[i]);
  return got;
}

void Session::chan(uint8_t channel, const uint8_t* data, size_t len) {
  env.chanWrite(channel, data, len);
}

void Session::serialWrite(const char* text) {
  size_t len = 0;
  while (text[len] != '\0') ++len;
  env.serialWrite(reinterpret_cast<const uint8_t*>(text), len);
}

size_t Session::serialRead(uint8_t* out, size_t len, uint32_t timeoutUs) {
  const size_t got = env.serialRead(out, len, timeoutUs);
  append(" S");
  for (size_t i = 0; i < got; ++i) append("%02X", out[i]);
  return got;
}

uint8_t Session::spi(uint8_t mosi) {
  const uint8_t miso = env.spiTransfer(mosi);
  append(" X%02X", miso);
  return miso;
}

void Session::line(uint8_t line, uint8_t level) { env.lineWrite(line, level); }

void Session::wait(uint32_t us) { env.delayMicros(us); }

void Session::end(ebdev::Device& dev) { env.dump(&dev); }

void Session::print(const char* variant, const char* name) {
  static char trace[8192];
  env.formatTrace(trace, sizeof(trace));
  printf("RUN %s %s BEGIN\n", variant, name);
  fputs(trace, stdout);
  printf("results%s\n", results);
  printf("stats events=%zu dropped=%u\n", env.eventCount(), env.dropped());
  printf("RUN %s %s END\n", variant, name);
}

// --- Temperature sensor (tests/native_env) -----------------------------------

void scenarioTemp(Session& s, ebdev::Device& dev) {
  const uint8_t addr = 0x48;
  s.begin(dev, addr);
  const uint8_t config[2] = {0x01, 0x05};
  s.write(addr, config, 2);
  const uint8_t raw300[2] = {0x01, 0x2C};
  s.chan(0, raw300, 2);
  const uint8_t pointer[1] = {0x00};
  s.write(addr, pointer, 1);
  uint8_t reading[2] = {0, 0};
  s.read(addr, reading, 2);
  s.end(dev);
}

// --- Environmental sensor (tests/catalog_devices) ------------------------------

void scenarioEnv(Session& s, ebdev::Device& dev) {
  const uint8_t addr = 0x76;
  s.begin(dev, addr);
  const uint8_t temp[3] = {0x7F, 0xE0, 0x00};
  s.chan(0, temp, 3);
  const uint8_t chipIdPointer[1] = {0xD0};
  s.write(addr, chipIdPointer, 1, false);
  uint8_t chipId[1] = {0};
  s.read(addr, chipId, 1);
  const uint8_t forced[2] = {0xF4, 0x25};
  s.write(addr, forced, 2);
  s.wait(1000);
  const uint8_t statusPointer[1] = {0xF3};
  s.write(addr, statusPointer, 1, false);
  uint8_t status[1] = {0};
  s.read(addr, status, 1);
  s.wait(7000);
  s.write(addr, statusPointer, 1, false);
  s.read(addr, status, 1);
  const uint8_t tempPointer[1] = {0xFA};
  s.write(addr, tempPointer, 1, false);
  uint8_t reading[3] = {0, 0, 0};
  s.read(addr, reading, 3);
  const uint8_t badPointer[1] = {0x42};
  s.write(addr, badPointer, 1, false);
  uint8_t nothing[1] = {0};
  s.read(addr, nothing, 1);
  s.end(dev);
}

// --- IMU with a FIFO (tests/units_sense) ----------------------------------------

void scenarioImu(Session& s, ebdev::Device& dev) {
  const uint8_t addr = 0x68;
  s.begin(dev, addr);
  const uint8_t value[2] = {0x12, 0x34};
  s.chan(0, value, 2);
  const uint8_t start[2] = {0x20, 0x01};
  s.write(addr, start, 2);
  // Back once the watermark is due: 8 samples at 2.5 ms is 20 ms.
  s.wait(21000);
  const uint8_t statusPointer[1] = {0x00};
  s.write(addr, statusPointer, 1);
  uint8_t status[2] = {0, 0};
  s.read(addr, status, 2);
  // Ask for more than is there: what comes back is the FIFO's business.
  const uint8_t fifoPointer[1] = {0x10};
  s.write(addr, fifoPointer, 1);
  uint8_t burst[40] = {0};
  s.read(addr, burst, 40);
  // Late: 16 slots at 2.5 ms is 40 ms, so 60 ms away loses samples.
  s.wait(60000);
  s.write(addr, statusPointer, 1);
  s.read(addr, status, 2);
  const uint8_t stop[2] = {0x20, 0x00};
  s.write(addr, stop, 2);
  s.end(dev);
}

// --- AT modem (tests/native_env) ---------------------------------------------

void scenarioModem(Session& s, ebdev::Device& dev) {
  s.begin(dev, 0);
  s.serialWrite("AT+S;");
  uint8_t reply[4] = {0, 0, 0, 0};
  s.serialRead(reply, 2, 10000);
  s.serialWrite("AT+X;");
  s.serialRead(reply, 3, 10000);
  s.end(dev);
}

// --- Environmental sensor, off the recorded path ------------------------------

void scenarioEnvStrayed(Session& s, ebdev::Device& dev) {
  const uint8_t addr = 0x76;
  s.begin(dev, addr);
  const uint8_t temp[3] = {0x7F, 0xE0, 0x00};
  s.chan(0, temp, 3);
  const uint8_t chipIdPointer[1] = {0xD0};
  s.write(addr, chipIdPointer, 1, false);
  uint8_t chipId[1] = {0};
  s.read(addr, chipId, 1);
  // A different command than the one recorded.
  const uint8_t forced[2] = {0xF4, 0x26};
  s.write(addr, forced, 2);
  s.wait(1000);
  // The result is read where the recording polled status.
  const uint8_t tempPointer[1] = {0xFA};
  s.write(addr, tempPointer, 1, false);
  uint8_t reading[3] = {0, 0, 0};
  s.read(addr, reading, 3);
  s.end(dev);
}

// --- Two parts on one bus -----------------------------------------------------

void scenarioTwoDevices(Session& s, ebdev::Device& temp, ebdev::Device& env) {
  s.begin(temp, 0x48);
  s.add(env, 0x76);
  const uint8_t config[2] = {0x01, 0x05};
  s.write(0x48, config, 2);
  const uint8_t chipIdPointer[1] = {0xD0};
  s.write(0x76, chipIdPointer, 1, false);
  uint8_t chipId[1] = {0};
  s.read(0x76, chipId, 1);
  const uint8_t raw300[2] = {0x01, 0x2C};
  s.chan(0, raw300, 2);
  const uint8_t forced[2] = {0xF4, 0x25};
  s.write(0x76, forced, 2);
  const uint8_t pointer[1] = {0x00};
  s.write(0x48, pointer, 1);
  uint8_t reading[2] = {0, 0};
  s.read(0x48, reading, 2);
  s.wait(8000);
  const uint8_t statusPointer[1] = {0xF3};
  s.write(0x76, statusPointer, 1, false);
  uint8_t status[1] = {0};
  s.read(0x76, status, 1);
  s.write(0x48, pointer, 1);
  s.read(0x48, reading, 2);
  s.end(temp);
  s.end(env);
}

// --- SPI flash ---------------------------------------------------------------------

void scenarioFlash(Session& s, ebdev::Device& flash) {
  s.begin(flash, 0);
  // Status: not busy, not write-enabled.
  s.line(0, 0);
  s.spi(0x05);
  s.spi(0x00);
  s.line(0, 1);
  // Write enable, then status shows it.
  s.line(0, 0);
  s.spi(0x06);
  s.line(0, 1);
  s.line(0, 0);
  s.spi(0x05);
  s.spi(0x00);
  s.line(0, 1);
  // Program two bytes at 0x10; the part is busy for a while.
  s.line(0, 0);
  s.spi(0x02);
  s.spi(0x10);
  s.spi(0xAB);
  s.spi(0xCD);
  s.line(0, 1);
  s.line(0, 0);
  s.spi(0x05);
  s.spi(0x00);
  s.line(0, 1);
  s.wait(4000);
  s.line(0, 0);
  s.spi(0x05);
  s.spi(0x00);
  s.line(0, 1);
  // Read them back.
  s.line(0, 0);
  s.spi(0x03);
  s.spi(0x10);
  s.spi(0x00);
  s.spi(0x00);
  s.line(0, 1);
  s.end(flash);
}

// --- A bus and a serial port at once ----------------------------------------------

void scenarioEnvAndModem(Session& s, ebdev::Device& env, ebdev::Device& modem) {
  s.begin(env, 0x76);
  s.add(modem, 0);
  const uint8_t temp[3] = {0x7F, 0xE0, 0x00};
  s.chan(0, temp, 3);
  const uint8_t chipIdPointer[1] = {0xD0};
  s.write(0x76, chipIdPointer, 1, false);
  uint8_t chipId[1] = {0};
  s.read(0x76, chipId, 1);
  s.serialWrite("AT+S;");
  const uint8_t forced[2] = {0xF4, 0x25};
  s.write(0x76, forced, 2);
  uint8_t reply[2] = {0, 0};
  s.serialRead(reply, 2, 10000);  // answered a tick later
  s.wait(7000);
  const uint8_t statusPointer[1] = {0xF3};
  s.write(0x76, statusPointer, 1, false);
  uint8_t status[1] = {0};
  s.read(0x76, status, 1);
  const uint8_t tempPointer[1] = {0xFA};
  s.write(0x76, tempPointer, 1, false);
  uint8_t reading[3] = {0, 0, 0};
  s.read(0x76, reading, 3);
  s.serialWrite("AT+X;");
  s.serialRead(reply, 2, 10000);
  s.end(env);
  s.end(modem);
}
