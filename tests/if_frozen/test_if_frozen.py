"""The frozen device interface: pinned surface plus standalone build."""

import re
import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
HEADER = SRC / "embedbench_device.h"

# The surface frozen as interface version 1 (docs/DEVICE_IF_FROZEN.ja.md).
# Adding a line here is an interface change and must follow the change
# rules in the header; editing or removing one is a version 2 decision.
FROZEN_SURFACE = [
    "constexpr uint16_t kDeviceInterfaceVersion = 1;",
    "constexpr size_t kFormatNameMaxLength = 19;",
    "constexpr size_t kChannelUnsupported = static_cast<size_t>(-1);",
    "enum I2cStatus : uint8_t {",
    "struct I2cTransfer {",
    "inline size_t frameBytes(size_t bits) {",
    "inline bool framePaddingClean(const uint8_t* data, size_t bits) {",
    "inline uint32_t schemaFingerprint(const char* layout) {",
    "class HostPort {",
    "virtual ~HostPort() {}",
    "virtual uint64_t nowMicros() = 0;",
    "virtual void lineOut(uint8_t line, uint8_t level) = 0;",
    "virtual bool serialOut(const uint8_t* data, size_t len) = 0;",
    "virtual bool frameOut(uint8_t bus, uint16_t format, const uint8_t* data,",
    "virtual uint16_t formatId(const char* name, uint32_t schema) {",
    "virtual uint32_t maxFrameBits(uint8_t bus) {",
    "class Device {",
    "virtual ~Device() {}",
    "virtual void reset() = 0;",
    "void attach(HostPort* port) { port_ = port; }",
    "virtual uint8_t i2cWrite(const uint8_t* data, size_t len,",
    "virtual size_t i2cRead(uint8_t* data, size_t len, const I2cTransfer& xfer) {",
    "virtual uint8_t spiTransfer(uint8_t mosi) {",
    "virtual void serialIn(const uint8_t* data, size_t len) {",
    "virtual void lineIn(uint8_t line, uint8_t level) {",
    "virtual void frameIn(uint8_t bus, uint16_t format, const uint8_t* data,",
    "virtual bool channelWrite(uint8_t channel, const uint8_t* data,",
    "virtual size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) {",
    "virtual void advanceTo(uint64_t nowUs) { (void)nowUs; }",
    "virtual size_t dump(char* out, size_t cap) {",
]


def declarations() -> list:
    """Every declaration line of the header, comments and bodies aside."""
    lines = []
    for raw in HEADER.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        if line.startswith(("virtual ", "constexpr ", "inline ", "class ",
                            "struct ", "enum ", "void attach")):
            lines.append(line)
    return lines


def test_surface_is_frozen():
    found = declarations()
    # Nothing in the frozen list may disappear or change spelling...
    for entry in FROZEN_SURFACE:
        assert entry in found, f"frozen surface changed or missing: {entry}"
    # ...and nothing may appear without being added to the list on purpose.
    unexpected = [l for l in found if l not in FROZEN_SURFACE]
    assert not unexpected, f"undeclared additions to the interface: {unexpected}"


def test_standalone_build_and_defaults():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    # Only the header is on the include path: no models, no environment.
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            f"-I{SRC}",
            str(HERE / "native" / "main.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # A device that overrides nothing answers as the header documents:
    # not on the bus, no bytes, idle SPI, no channels, empty dump.
    assert ("defaults version=1 i2c_write=2 i2c_read=0 spi=FF "
            "channel_write=0 channel_read_unsupported=1 dump=0 dump_nul=1"
            ) in result.stdout
    # An environment that routes no frames refuses them and reports id 0.
    assert "port_defaults frame_out=0 format_id=0 max_bits=0" in result.stdout
    # Helpers: byte counts, padding checks (including the null cases), the
    # name limit, and a deterministic fingerprint.
    assert ("helpers bytes0=0 bytes1=1 bytes8=1 bytes9=2 clean=1 dirty=0 "
            "null0=1 null8=0 name_max=19 fp_same=1 fp_diff=1") in result.stdout
    assert "NATIVE done" in result.stdout


def test_header_declares_the_freeze():
    text = HEADER.read_text()
    assert "FROZEN — interface version 1" in text
    assert re.search(r"kDeviceInterfaceVersion = 1;", text)
