"""X65: the other three shims, and the sigrok text format.

CaptureSerial on the modem's port, CaptureSPI on the flash, CaptureLines
on the sensor's DRDY next to CaptureWire — one session, both views (the
shims' lines and the host environment's record), checked on the host and
compiled for an ESP32. The shims' lines then go through the tape and
scaffold generators the way a board's would.
"""

import importlib.util
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
TOOLS = ROOT / "devices" / "tools"


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def test_streams_on_host(dut):
    dut.expect("TEST start capture_streams", timeout=10)
    block = dut.expect(re.compile(rb"CAPTURE BEGIN\r?\n(.*?)CAPTURE END", re.S),
                       timeout=10)
    lines = [l.strip()[4:] for l in block.group(1).decode().splitlines()
             if l.startswith("CAP ")]
    dut.expect("values reply=OK status0=00 status1=02 polls=8 raw=FE0000 "
               "drdy=1", timeout=10)
    dut.expect("TEST done", timeout=10)

    def kinds(prefix):
        return [l for l in lines if re.match(r"\d+ " + prefix, l)]

    # Serial: the command as text, the reply a tick later, noticed by the
    # application's poll.
    serial = kinds("(uart|dev)\\.tx")
    assert re.fullmatch(r"\d+ uart\.tx AT\+S;", serial[0])
    assert re.fullmatch(r"\d+ dev\.tx OK", serial[1])
    t_cmd, t_reply = int(serial[0].split()[0]), int(serial[1].split()[0])
    assert 1000 <= t_reply - t_cmd <= 2000
    # SPI: one line per transfer call, MOSI and MISO whole.
    spi = kinds("spi\\.xfer")
    assert [l.split(" ", 1)[1] for l in spi] == [
        "spi.xfer mosi=05 miso=FF", "spi.xfer mosi=00 miso=00",
        "spi.xfer mosi=06 miso=FF",
        "spi.xfer mosi=05 miso=FF", "spi.xfer mosi=00 miso=02",
    ]
    # The line: DRDY rose while the application was polling, and the
    # bus-side capture now carries it.
    drdy = kinds("gpio\\.inject")
    assert len(drdy) == 1 and drdy[0].endswith("gpio.inject pin=27 val=1")

    out = HERE / "output"
    out.mkdir(exist_ok=True)
    trace = out / "shim.trace"
    trace.write_text("\n".join(lines) + "\n")
    tape = load("trace2tape")
    events = load("trace2regtable").parse(lines)
    steps, skipped = tape.build(events, None, ("serial",))
    assert [(s.kind, bytes(s.data)) for s in steps] == [
        ("kSerialIn", b"AT+S;"), ("kSerialOut", b"OK")]
    assert 1000 <= steps[1].delay <= 2000 and skipped == []
    steps, skipped = tape.build(events, None, ("spi",))
    # Every transfer call is a step of its own on a board — the wrapper
    # cannot see chip select — and the bytes still match one by one.
    assert [(s.length, bytes(s.data)) for s in steps] == [
        (1, b"\x05\xFF"), (1, b"\x00\x00"), (1, b"\x06\xFF"),
        (1, b"\x05\xFF"), (1, b"\x00\x02")]
    # The scaffold sees the line this time: what X64 said a bus-side
    # capture lacks, CaptureLines adds back.
    scaffold = load("trace2regtable")
    a = scaffold.analyze(events, 0x76)
    assert sorted(a.regs) == [0xF3, 0xF4, 0xFA]
    assert a.regs[0xF3].volatile and a.require_rs
    line_items = [t for t in a.todo if t.startswith("line 27 -> 1 at ")]
    assert len(line_items) == 1 and "after write F4=25" in line_items[0]


def test_shims_compile_for_esp32():
    if shutil.which("arduino-cli") is None:
        pytest.skip("arduino-cli not on PATH")
    cores = subprocess.run(["arduino-cli", "core", "list"], capture_output=True,
                           text=True).stdout
    if "esp32:esp32" not in cores:
        pytest.skip("esp32:esp32 core not installed")
    out = HERE / "output" / "esp32"
    out.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        ["arduino-cli", "compile", "--fqbn", "esp32:esp32:esp32",
         "--library", str(ROOT / "capture"), "--output-dir", str(out),
         "--warnings", "all", str(HERE / "esp32_check")],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr[-3000:]
    assert "Capture" not in result.stderr, result.stderr[-3000:]


SIGROK_TEXT = """\
100-110 i2c-1: Start
111-200 i2c-1: Address write: 76
201-210 i2c-1: ACK
211-300 i2c-1: Data write: D0
301-310 i2c-1: ACK
311-320 i2c-1: Start repeat
321-410 i2c-1: Address read: 76
411-420 i2c-1: ACK
421-510 i2c-1: Data read: 60
511-520 i2c-1: NACK
521-530 i2c-1: Stop
"""


def test_la2trace_sigrok():
    la = load("la2trace")
    # 1 MHz sample rate: sample numbers are microseconds. The decoder's
    # default address_format=shifted prints the 7-bit address.
    assert la.convert(SIGROK_TEXT, samplerate=1_000_000) == [
        "111 i2c.req addr=76 data=D0 stop=0",
        "111 i2c.resp status=0",
        "321 i2c.rd.req addr=76 req=1 stop=1 rs",
        "321 i2c.rd.resp len=1 data=60",
    ]
    # Without sample numbers the lines carry no time and still parse.
    bare = "\n".join(l.split(" ", 1)[1] for l in SIGROK_TEXT.splitlines())
    lines = la.convert(bare)
    assert lines[0] == "i2c.req addr=76 data=D0 stop=0"
    scaffold = load("trace2regtable")
    a = scaffold.analyze(scaffold.parse(lines), 0x76)
    assert a.regs[0xD0].reset == b"\x60" and a.require_rs
    # Run with address_format=unshifted, the decoder prints the address
    # byte with its R/W bit: EC to write, ED to read.
    raw = SIGROK_TEXT.replace("Address write: 76", "Address write: EC").replace(
        "Address read: 76", "Address read: ED")
    assert la.convert(raw, samplerate=1_000_000, address_format="unshifted") == \
        la.convert(SIGROK_TEXT, samplerate=1_000_000)


# Real captures of real parts, published in sigrok-dumps (the sigrok
# project's collection of example captures), decoded here by sigrok-cli.
# This is the check X64 left open — the format against a live decoder —
# done without a board: the analyzer output is the analyzer output.
DUMPS = "https://raw.githubusercontent.com/sigrokproject/sigrok-dumps/master/i2c/"
REAL_CAPTURES = {
    # a ROHM BH1750 light sensor, ADDR grounded (0x23), fx2lafw clone, 500 kHz
    "bh1750": ("rohm_bh1750/bh1750_hresolutionmode.sr", 500_000),
    # a Sensirion SHT31 humidity sensor at 0x45, USBee SX, 8 MHz
    "sht31": ("sensirion_sht3x/sensirion_sht31_25rh_28rh.sr", 8_000_000),
}
CLASSES = ("start:repeat-start:stop:ack:nack:address-read:address-write:"
           "data-read:data-write")


def decode_real_capture(name):
    import urllib.request
    if shutil.which("sigrok-cli") is None:
        pytest.skip("sigrok-cli not on PATH")
    path, samplerate = REAL_CAPTURES[name]
    out = HERE / "output" / "sigrok"
    out.mkdir(parents=True, exist_ok=True)
    sr = out / f"{name}.sr"
    if not sr.exists():
        try:
            urllib.request.urlretrieve(DUMPS + path, sr)
        except OSError as e:
            pytest.skip(f"sigrok-dumps not reachable: {e}")
    result = subprocess.run(
        ["sigrok-cli", "-i", str(sr), "-P", "i2c:scl=SCL:sda=SDA",
         "-A", f"i2c={CLASSES}", "--protocol-decoder-samplenum"],
        capture_output=True, text=True, check=True)
    return result.stdout, samplerate


def test_la2trace_real_bh1750():
    text, samplerate = decode_real_capture("bh1750")
    la = load("la2trace")
    lines = la.convert(text, samplerate=samplerate)
    # Power on, two MTreg halves under one repeated start, the mode
    # command twice, then the 2-byte reading — at the address the README
    # gives, 0x23, which the decoder prints as the 7-bit address.
    assert [l.split(" ", 1)[1] for l in lines] == [
        "i2c.req addr=23 data=01 stop=1", "i2c.resp status=0",
        "i2c.req addr=23 data=42 stop=0", "i2c.resp status=0",
        "i2c.req addr=23 data=65 stop=0 rs", "i2c.resp status=0",
        "i2c.req addr=23 data=20 stop=1 rs", "i2c.resp status=0",
        "i2c.req addr=23 data=20 stop=1", "i2c.resp status=0",
        "i2c.rd.req addr=23 req=2 stop=1", "i2c.rd.resp len=2 data=0029",
    ]
    times = [int(l.split()[0]) for l in lines]
    assert times[0] == 2014 and times[-1] == 127614  # 500 kHz: 2 us a sample
    scaffold = load("trace2regtable")
    a = scaffold.analyze(scaffold.parse(lines), 0x23)
    # A part with commands rather than registers still fits: the last
    # command byte selects the 2-byte reading.
    assert sorted(a.regs) == [0x20] and a.regs[0x20].reset == b"\x00\x29"


def test_la2trace_real_sht31():
    text, samplerate = decode_real_capture("sht31")
    la = load("la2trace")
    lines = la.convert(text, samplerate=samplerate)
    # Twelve seconds of single-shot measurements: a read, then eleven
    # rounds of "write the command 0x2400 or 0x2416, wait a second under
    # a repeated start, read six bytes".
    assert len(lines) == 48
    assert lines[0].endswith("i2c.rd.req addr=45 req=6 stop=1")
    assert lines[1].endswith("i2c.rd.resp len=6 data=67A2E4487FE9")
    assert lines[2].endswith("i2c.req addr=45 data=2400 stop=0")
    assert lines[4].endswith("i2c.rd.req addr=45 req=6 stop=1 rs")
    assert sum(1 for l in lines if " rs" in l) == 11
    scaffold = load("trace2regtable")
    a = scaffold.analyze(scaffold.parse(lines), 0x45)
    # Every round writes the same command and reads a fresh measurement:
    # the table cannot say that, and the TODO list says so once per change.
    assert sorted(a.regs) == [0x00, 0x24]
    assert a.regs[0x24].volatile
    assert all("a command, not stored contents" in t for t in a.todo)
    assert len(a.todo) == 10  # eleven reads, ten changes between them
