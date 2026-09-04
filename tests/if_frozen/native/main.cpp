// The frozen interface must stand on its own: this translation unit
// includes nothing but the header and instantiates the two abstractions,
// proving there is no hidden dependency and that a device needs to
// override nothing beyond reset().
#include <stdio.h>

#include <embedbench_device.h>

namespace {

// Minimal environment: only the three pure virtuals must be written.
struct MinimalPort : public ebdev::HostPort {
  uint64_t nowMicros() override { return 0; }
  void lineOut(uint8_t, uint8_t) override {}
  bool serialOut(const uint8_t*, size_t) override { return true; }
};

// Minimal device: only reset() must be written.
struct MinimalDevice : public ebdev::Device {
  void reset() override {}
};

}  // namespace

int main() {
  printf("NATIVE start\n");
  MinimalPort port;
  MinimalDevice device;
  device.attach(&port);
  device.reset();

  // Defaults of an unimplemented device, as the header promises. Each
  // call is sequenced into a variable first: the evaluation order of
  // printf arguments is unspecified, and dump() writes into `text`.
  const ebdev::I2cTransfer plain = {true, false};
  uint8_t buf[2] = {0};
  char text[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
  const uint8_t status = device.i2cWrite(buf, 2, plain);
  const size_t readLen = device.i2cRead(buf, 2, plain);
  const uint8_t miso = device.spiTransfer(0x5A);
  const bool applied = device.channelWrite(0, buf, 2);
  const bool unsupported =
      device.channelRead(0, buf, sizeof(buf)) == ebdev::kChannelUnsupported;
  const size_t dumpLen = device.dump(text, sizeof(text));
  const bool dumpTerminated = text[0] == '\0';
  printf("defaults version=%u i2c_write=%u i2c_read=%zu spi=%02X "
         "channel_write=%d channel_read_unsupported=%d dump=%zu dump_nul=%d\n",
         ebdev::kDeviceInterfaceVersion, status, readLen, miso,
         applied ? 1 : 0, unsupported ? 1 : 0, dumpLen,
         dumpTerminated ? 1 : 0);

  // Defaults of an environment that routes no frames.
  const bool framed = port.frameOut(0, 1, buf, 8);
  const uint16_t id = port.formatId("acme.x.1", 1);
  const uint32_t maxBits = port.maxFrameBits(0);
  printf("port_defaults frame_out=%d format_id=%u max_bits=%u\n",
         framed ? 1 : 0, id, maxBits);

  // Frozen helpers.
  const uint8_t clean[2] = {0xAB, 0xC0};
  const uint8_t dirty[2] = {0xAB, 0xCF};
  printf("helpers bytes0=%zu bytes1=%zu bytes8=%zu bytes9=%zu clean=%d "
         "dirty=%d null0=%d null8=%d name_max=%zu fp_same=%d fp_diff=%d\n",
         ebdev::frameBytes(0), ebdev::frameBytes(1), ebdev::frameBytes(8),
         ebdev::frameBytes(9), ebdev::framePaddingClean(clean, 12) ? 1 : 0,
         ebdev::framePaddingClean(dirty, 12) ? 1 : 0,
         ebdev::framePaddingClean(nullptr, 0) ? 1 : 0,
         ebdev::framePaddingClean(nullptr, 8) ? 1 : 0,
         ebdev::kFormatNameMaxLength,
         ebdev::schemaFingerprint("u8 a,u8 b") ==
                 ebdev::schemaFingerprint("u8 a,u8 b")
             ? 1 : 0,
         ebdev::schemaFingerprint("u8 a,u8 b") !=
                 ebdev::schemaFingerprint("u8 b,u8 a")
             ? 1 : 0);

  printf("NATIVE done\n");
  return 0;
}
