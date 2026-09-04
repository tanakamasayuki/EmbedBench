"""Re-entrancy contract on every device path plus the deferral capacity."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "paths_model.h", HERE / "paths_model.cpp"]:
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
            str(HERE / "paths_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert "paths pulses=5 frames=1 dump=<paths reads=1 cmds=1 max_depth=1>" \
        in result.stdout
    assert "NATIVE done" in result.stdout


def test_reentry_paths(dut):
    dut.expect("TEST start reentry_paths", timeout=10)
    # lineIn (1) + frameIn (1) + burst (4 of 5) deferred interrupts, one
    # deferred frame delivery, one dropped effect, and the device never
    # re-entered on any path.
    dut.expect("values irq_count=4 deferred_isrs=6 deferred_frames=1 dropped=1 "
               "device_depth=1 capacity=4", timeout=10)
    # lineIn path: the pulse is recorded inside lineIn, the ISR runs after.
    dut.expect("02 000000 main app gpio.write pin=4 val=1", timeout=10)
    dut.expect("03 000000 main dev gpio.inject pin=27 0->1 match=1", timeout=10)
    dut.expect("04 000000 main dev gpio.inject pin=27 1->0 match=0", timeout=10)
    dut.expect("05 000000 isr core isr.enter pin=27", timeout=10)
    dut.expect("10 000000 isr core isr.exit pin=27", timeout=10)
    # advanceTo path: the status frame is recorded inside advanceTo; the
    # application shim runs after it and its command reaches frameIn,
    # whose pulse is again deferred until frameIn returns.
    dut.expect("11 001000 tick dev dev.frame bus=0 fmt=acme.stat.1 bits=16 data=0001",
               timeout=10)
    dut.expect("12 001000 tick app frame.tx bus=0 fmt=acme.cmd.1 bits=8 data=01",
               timeout=10)
    dut.expect("13 001000 tick dev gpio.inject pin=27 0->1 match=1", timeout=10)
    dut.expect("15 001000 isr core isr.enter pin=27", timeout=10)
    dut.expect("20 001000 isr core isr.exit pin=27", timeout=10)
    # Burst path: the fifth interrupt exceeds the deferral capacity and is
    # diagnosed, never delivered re-entrantly.
    dut.expect("22 001000 main dir chan.write chan=0 data=05", timeout=10)
    dut.expect("35 001000 main diag diag.deferred_full kind=isr pin=27", timeout=10)
    dut.expect("37 001000 main dev i2c.rd.resp len=2 data=0102 re=25", timeout=10)
    dut.expect("46 001000 main dir dump paths reads=3 cmds=1 max_depth=1",
               timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
