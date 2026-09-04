"""The portable device interface: pure-C++ proof plus host-side identity."""

import re
import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
MODELS = HERE.parent / "common_models" / "src"
PURE_SOURCES = [
    SRC / "embedbench_device.h",
    MODELS / "temp_model.h",
    MODELS / "temp_model.cpp",
    MODELS / "modem_model.h",
    MODELS / "modem_model.cpp",
]
FORBIDDEN = [
    "Arduino.h",
    "HostBus",
    "HostClock",
    "HostUart",
    "HostInterrupt",
    "Wire.h",
    "embedbench_draft",
]


def loc(path: Path) -> int:
    lines = path.read_text().splitlines()
    return len([
        l for l in lines if l.strip() and not l.strip().startswith("//")
    ])


def region_loc(path: Path, name: str) -> int:
    match = re.search(
        rf"// \[{re.escape(name)} begin\]\n(.*?)// \[{re.escape(name)} end\]",
        path.read_text(),
        re.DOTALL,
    )
    assert match, f"missing markers for {name}"
    return len([
        l for l in match.group(1).splitlines()
        if l.strip() and not l.strip().startswith("//")
    ])


def test_native_portability():
    # The fixed surface and the models must not include any platform
    # header (comments may mention them; #include lines may not).
    for source in PURE_SOURCES:
        includes = [
            l for l in source.read_text().splitlines()
            if l.strip().startswith("#include")
        ]
        for token in FORBIDDEN:
            for line in includes:
                assert token not in line, (
                    f"{source.name} includes {token}: {line.strip()}")

    # Build with plain g++, C++11, warnings as errors — no Arduino at all.
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{MODELS}",
            str(HERE / "native" / "main.cpp"),
            str(MODELS / "temp_model.cpp"),
            str(MODELS / "modem_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert "NATIVE start" in result.stdout
    assert ("temp status=0 line=0:1 line_calls=1 read_len=2 read=00FA "
            "dump=<temp=00FA cfg=05>") in result.stdout
    assert "modem at=<OK> at_t=0" in result.stdout
    assert ("modem calls999=1 out=<OK> out_t=1000 "
            "dump=<modem replies=2 pending=0>") in result.stdout
    assert "NATIVE done" in result.stdout

    sizes = {
        "device_if_header": loc(SRC / "embedbench_device.h"),
        "temp_model": loc(MODELS / "temp_model.h") +
                      loc(MODELS / "temp_model.cpp"),
        "modem_model": loc(MODELS / "modem_model.h") +
                       loc(MODELS / "modem_model.cpp"),
        "adapter": region_loc(HERE / "device_if.ino", "adapter"),
    }
    # Growth log (effective LOC, blank and comment lines excluded): 58 at
    # X23, +4 lineIn (X24), +10 frameOut/frameIn (X25), +8 bus ids and
    # formatId interning (X26), +4 negotiated maxFrameBits (X27), +22 for
    # the first contract review (I2cStatus/I2cTransfer, frame helpers, bool
    # frameOut/channelWrite, schema; X30-X34), +14 for the second review
    # (name length limit, kChannelUnsupported, safe frame helpers,
    # schemaFingerprint; X35-X37). The third review (X38-X40) changed
    # contract text only, so the header held at 120 while the modem grew a
    # binary reply branch and the adapter shrank to a binary-safe
    # serialOut.
    print(f"LOC {sizes}")
    assert sizes == {
        "device_if_header": 120,
        "temp_model": 64,
        "modem_model": 80,
        "adapter": 33,
    }


def test_device_if(dut):
    dut.expect("TEST start device_if", timeout=10)
    dut.expect("values t1=300 spins=3 reply=OK elapsed=1000", timeout=10)

    dut.expect("01 000000 main app i2c.req addr=48 data=0105 stop=1", timeout=10)
    dut.expect("02 000000 main dev i2c.resp status=0 re=1", timeout=10)
    dut.expect("03 000000 main app int.attach pin=27 trig=1", timeout=10)
    dut.expect("04 000000 tick dir chan.write chan=0 data=012C", timeout=10)
    # The device reacted to the injection by raising DRDY through its
    # port: recorded as a dev-origin injection, then the ISR runs.
    dut.expect("05 000000 tick dev gpio.inject pin=27 0->1 match=1",
               timeout=10)
    dut.expect("06 000000 isr core isr.enter pin=27", timeout=10)
    dut.expect("07 000000 isr app gpio.write pin=5 val=1", timeout=10)
    dut.expect("08 000000 isr core isr.exit pin=27", timeout=10)
    dut.expect("09 000000 main app i2c.req addr=48 data=00 stop=1", timeout=10)
    dut.expect("10 000000 main dev i2c.resp status=0 re=9", timeout=10)
    dut.expect("11 000000 main app i2c.rd.req addr=48 req=2 stop=1", timeout=10)
    dut.expect("12 000000 main dev i2c.rd.resp len=2 data=012C re=11",
               timeout=10)
    dut.expect(r"13 000000 main app uart.tx AT\+S;", timeout=10)
    # The modem's latency reply arrives via advanceTo at the next tick.
    dut.expect("14 001000 tick dev dev.tx OK", timeout=10)
    dut.expect("15 001000 main app uart.rx O", timeout=10)
    dut.expect("16 001000 main app uart.rx K", timeout=10)
    dut.expect("17 001000 main dir dump temp=012C cfg=05", timeout=10)
    dut.expect("18 001000 main dir dump modem replies=1 pending=0",
               timeout=10)

    dut.expect("stats events=18 dropped=0 resp_lines=3 diag=0", timeout=10)
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
