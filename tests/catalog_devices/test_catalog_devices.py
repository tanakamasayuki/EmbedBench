"""Catalog devices at work: an environmental sensor and a GPS."""

import re
import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
MODELS = HERE.parent.parent / "devices" / "src"
ENV = HERE.parent / "common_env"


def test_native_catalog():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{MODELS}", f"-I{ENV}",
            str(HERE / "native" / "main.cpp"),
            str(ENV / "nenv.cpp"),
            str(MODELS / "env_sensor_model.cpp"),
            str(MODELS / "gps_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # The part identifies itself, reports "measuring" one tick in and
    # "done" after its 7.5 ms, and hands back the value the world showed
    # it. The unmapped register cannot be refused by a return value, so
    # the sensor says so through the diagnostic path instead (bad_reg=1).
    assert ("sensor chip=60 early=08 late=00 read_len=3 data=FE0000 "
            "bad_len=0 dump=<env raw=7FE000 latched=7FE000 meas=1 bad_reg=1>"
            ) in result.stdout
    # The GPS emits on its own schedule once started, with the checksum
    # the protocol defines, and rejects a command it does not know.
    assert "gps first=$GPGGA,10,1*66" in result.stdout
    assert "dump=<gps run=1 fix=1 sent=2 rejected=1>" in result.stdout
    assert "NATIVE done" in result.stdout


def test_host_catalog(dut):
    dut.expect("TEST start catalog_devices", timeout=10)
    # Unmodified Arduino code: identify the part, start a forced
    # measurement, poll the status register, read the result, then read
    # one NMEA sentence.
    # Escaped literally: the payloads carry regex characters and the log
    # shows line endings as backslash escapes.
    dut.expect(re.escape(r"values chip=60 polls=8 raw=FE0000 ready=1 "
                         r"sentence=$GPGGA,10,1*66"), timeout=10)
    dut.expect("02 000000 main app i2c.req addr=76 data=D0 stop=0", timeout=10)
    dut.expect("05 000000 main dev i2c.rd.resp len=1 data=60 re=4", timeout=10)
    dut.expect("06 000000 main app i2c.req addr=76 data=F425 stop=1",
               timeout=10)
    # The status poll repeats the same four lines eight times. Rather
    # than spend 32 slots on them and lose the end of the run (which is
    # what happened before X53), the round is kept once with a count and
    # the moment the last copy happened.
    dut.expect("08 000000 main app i2c.req addr=76 data=F3 stop=0 x8..007000",
               timeout=10)
    dut.expect("11 000000 main dev i2c.rd.resp len=1 data=08 re=10 "
               "x8..007000", timeout=10)
    # The measurement finishes at 7500 us, the time the part states —
    # not rounded up to the environment's next 1000 us boundary.
    dut.expect("40 007500 tick dev gpio.inject pin=27 0->1 match=0",
               timeout=10)
    dut.expect("44 008500 main dev i2c.rd.resp len=1 data=00 re=43",
               timeout=10)
    dut.expect("48 008500 main dev i2c.rd.resp len=3 data=FE0000 re=47",
               timeout=10)
    # Line-based traffic stays readable in the log: the terminators are
    # escaped rather than turning the payload into a checksum.
    dut.expect(re.escape(r"49 008500 main app uart.tx START\n"), timeout=10)
    dut.expect(re.escape(r"50 010500 tick dev dev.tx $GPGGA,10,1*66\r\n"),
               timeout=10)
    # The conclusion of the run survives, which is the point: the sensor
    # and GPS dumps are the lines a test asserts on, and before X53 they
    # were the ones the polling loop pushed out of the buffer.
    dut.expect("67 010500 main dir dump env raw=7FE000 latched=7FE000 "
               "meas=1 bad_reg=0", timeout=10)
    dut.expect("68 010500 main dir dump gps run=1 fix=1 sent=1 rejected=0",
               timeout=10)
    # Nothing is lost now: the whole run fits once the repeats are folded.
    dut.expect("stats events=38 dropped=0 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
