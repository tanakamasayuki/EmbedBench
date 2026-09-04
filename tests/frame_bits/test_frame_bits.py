"""Frame bit packing (MSB-first, clean padding, empty frames) and atomicity."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "ir_model.h", HERE / "ir_model.cpp"]:
        includes = [
            l for l in source.read_text().splitlines()
            if l.strip().startswith("#include")
        ]
        for token in ["Arduino.h", "Host", "embedbench_draft"]:
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
            str(HERE / "ir_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert ("frames first=<ir value=ABC frames=1 triggers=0 unsent=0> "
            "rejected_padding=1 refused_oversize=1 accepted=0 "
            "dump=<ir value=123 frames=2 triggers=1 unsent=1>") in result.stdout
    assert "NATIVE done" in result.stdout


def test_frame_bits(dut):
    dut.expect("TEST start frame_bits", timeout=10)
    dut.expect("01 000000 main app frame.tx bus=0 fmt=acme.ir.1 bits=12 data=ABC0",
               timeout=10)
    dut.expect("02 000000 main app frame.tx bus=0 fmt=acme.ir.1 bits=12 data=1230",
               timeout=10)
    # Dirty padding is a contract violation: refused, never delivered.
    dut.expect("03 000000 main diag diag.frame_padding bus=0 bits=12", timeout=10)
    # An empty frame is a valid trigger.
    dut.expect("04 000000 main app frame.tx bus=0 fmt=acme.ir.1 bits=0 empty",
               timeout=10)
    dut.expect("05 000000 main dir chan.write chan=0 data=01", timeout=10)
    # The atomic 128-bit status frame does not fit the 64-bit capacity.
    dut.expect("06 000000 main diag diag.frame_oversize bus=0 bits=128 max=64",
               timeout=10)
    dut.expect("07 000000 main dir dump ir value=123 frames=2 triggers=1 unsent=1",
               timeout=10)
    dut.expect("stats events=7 dropped=0 diag=2", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
