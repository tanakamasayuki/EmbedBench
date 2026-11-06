"""the entrance from a real board.

CaptureWire prints, on the board, the same event lines the host
environment records — checked here on the host, where both views of one
session exist and can be compared, and compiled for an ESP32 to show it
builds where it is meant to run.
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


def test_shim_on_host(dut):
    dut.expect("TEST start capture_shim", timeout=10)
    block = dut.expect(re.compile(rb"CAPTURE BEGIN\r?\n(.*?)CAPTURE END", re.S),
                       timeout=10)
    lines = [l.strip() for l in block.group(1).decode().splitlines()
             if l.startswith("CAP ")]
    # Eleven transfers, two lines each, whole payloads, the repeated start
    # marked on the reads that continued a pointer write.
    assert len(lines) == 22
    assert re.match(r"CAP \d+ i2c\.req addr=76 data=D0 stop=0$", lines[0])
    assert re.match(r"CAP \d+ i2c\.resp status=0$", lines[1])
    assert re.match(r"CAP \d+ i2c\.rd\.req addr=76 req=1 stop=1 rs$", lines[2])
    assert re.match(r"CAP \d+ i2c\.rd\.resp len=1 data=60$", lines[3])
    assert re.match(r"CAP \d+ i2c\.req addr=76 data=F425 stop=1$", lines[4])
    assert re.match(r"CAP \d+ i2c\.rd\.resp len=3 data=FE0000$", lines[17])
    assert re.match(r"CAP \d+ i2c\.rd\.resp len=0 data=$", lines[21])
    # The board's clock is behind the lines: the second status poll is at
    # least the 7 ms the application waited after the first.
    times = [int(l.split()[1]) for l in lines]
    assert times[13] - times[9] >= 7000

    dut.expect("values chip=60 early=08 late=00 read_len=3 data=FE0000 "
               "bad_len=0 transfers=11", timeout=10)
    # The host environment saw the same session underneath the shim.
    dut.expect("i2c.req addr=76 data=F425 stop=1", timeout=10)
    dut.expect("stats events=26 dropped=0 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)

    # The shim's lines are a capture: the scaffold tool reads them as it
    # reads a host trace, and infers the same table — with one difference
    # that is the point of the comparison: a bus-side capture cannot see
    # the DRDY line, the world injection, or the part's notes, so the TODO
    # list shrinks from four items to the one the bus itself shows.
    out = HERE / "output"
    out.mkdir(exist_ok=True)
    trace = out / "shim.trace"
    trace.write_text("\n".join(lines) + "\n")
    tool = load("trace2regtable")
    a = tool.analyze(tool.parse(lines), 0x76)
    assert sorted(a.regs) == [0xD0, 0xF3, 0xF4, 0xFA]
    assert a.regs[0xD0].reset == b"\x60"
    assert a.regs[0xF3].reset == b"\x08" and a.regs[0xF3].volatile
    assert a.regs[0xF4].writable
    assert a.regs[0xFA].reset == b"\xFE\x00\x00"
    assert a.require_rs
    assert [t for t in a.todo] == [
        "register F3 changed 08 -> 00 with only time passing (%d us)"
        % (times[13] - times[9])]


def test_shim_compiles_for_esp32():
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
    assert "CaptureWire.h" not in result.stderr, result.stderr[-3000:]
