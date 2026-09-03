"""Generic frame path: protocols without a dedicated port, no bit-banging."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "node_model.h", HERE / "node_model.cpp"]:
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
            str(HERE / "node_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert ("node foreign=0 at999=0 frames=1 fmt=2 bits=16 data=0401 "
            "dump=<node power=1 pending=0>") in result.stdout
    assert "NATIVE done" in result.stdout


def test_frame_port(dut):
    dut.expect("TEST start frame_port", timeout=10)
    dut.expect("values got=1 telemetry=0401", timeout=10)
    # The foreign-address frame is recorded but ignored by the device;
    # interpretation of the bits belongs to the device, not the log.
    dut.expect("01 000000 main app frame.tx fmt=1 bits=16 data=0508",
               timeout=10)
    dut.expect("02 000000 main app frame.tx fmt=1 bits=16 data=0408",
               timeout=10)
    dut.expect("03 001000 tick dev dev.frame fmt=2 bits=16 data=0401",
               timeout=10)
    dut.expect("04 002000 main dir dump node power=1 pending=0", timeout=10)
    dut.expect("stats events=4 dropped=0 diag=0", timeout=10)
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
