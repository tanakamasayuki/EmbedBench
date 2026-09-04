"""Re-entrancy contract: an IRQ raised inside a device call is deferred."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "irq_model.h", HERE / "irq_model.cpp"]:
        includes = [
            l for l in source.read_text().splitlines()
            if l.strip().startswith("#include")
        ]
        for token in ["Arduino.h", "Host", "Wire.h", "embedbench_draft"]:
            for line in includes:
                assert token not in line, (
                    f"{source.name} includes {token}: {line.strip()}")

    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{HERE}",
            str(HERE / "native" / "main.cpp"),
            str(HERE / "irq_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # Running the ISR at once re-enters the device (depth 2); deferring it
    # keeps every device call at depth 1 with the same number of reads.
    assert "immediate isr_runs=1 max_depth=2 dump=<irq reads=2 max_depth=2>" \
        in result.stdout
    assert "deferred isr_runs=1 max_depth=1 dump=<irq reads=2 max_depth=1>" \
        in result.stdout
    assert "NATIVE done" in result.stdout


def test_reentry(dut):
    dut.expect("TEST start reentry", timeout=10)
    # The device is never re-entered (depth 1), yet the application's own
    # value is garbage: the ISR ran when the bus transfer completed — before
    # Wire.requestFrom() returned — and its nested transaction clobbered the
    # shared Wire receive buffer. That is the same bug class an ISR doing
    # blocking bus I/O has on real hardware, surfaced deterministically.
    dut.expect("values app=FFFF isr_reads=1 deferred=1 device_depth=1",
               timeout=10)
    dut.expect("01 000000 main app int.attach pin=27 trig=1", timeout=10)
    dut.expect("02 000000 main app i2c.req addr=48 data=00 stop=1", timeout=10)
    dut.expect("03 000000 main dev i2c.resp status=0 re=2", timeout=10)
    dut.expect("04 000000 main app i2c.rd.req addr=48 req=2 stop=1", timeout=10)
    # The line change is recorded where it happened (inside the read)...
    dut.expect("05 000000 main dev gpio.inject pin=27 0->1 match=1", timeout=10)
    dut.expect("06 000000 main dev i2c.rd.resp len=2 data=0102 re=4", timeout=10)
    # ...but the ISR runs only after the read completed, as fresh calls.
    dut.expect("07 000000 isr core isr.enter pin=27", timeout=10)
    dut.expect("08 000000 isr app i2c.req addr=48 data=00 stop=1", timeout=10)
    dut.expect("09 000000 isr dev i2c.resp status=0 re=8", timeout=10)
    dut.expect("10 000000 isr app i2c.rd.req addr=48 req=2 stop=1", timeout=10)
    dut.expect("11 000000 isr dev i2c.rd.resp len=2 data=0102 re=10", timeout=10)
    dut.expect("12 000000 isr core isr.exit pin=27", timeout=10)
    dut.expect("13 000000 main dir dump irq reads=2 max_depth=1", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
