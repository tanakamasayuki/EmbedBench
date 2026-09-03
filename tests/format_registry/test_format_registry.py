"""Format identity: numeric collision, interned names, strings, no registry."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "named_node_model.h", HERE / "named_node_model.cpp"]:
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
            str(HERE / "named_node_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # Fixed numbers: the vendor frame silently switches the node off.
    assert "numeric power_before=1 power_after=0" in result.stdout
    # Interned names: environment-local ids, no collision, idempotent.
    assert ("interned vendor_id=1 cmd_id=2 again=2 after_vendor=0 frames=1 "
            "fmt=3 dump=<node power=1 pending=0>") in result.stdout
    # Integer compares after one resolution vs one strcmp per frame.
    assert "cost interned_100=0 strings_100=100" in result.stdout
    # No registry: formatId=0 keeps the device inert instead of guessing.
    assert "noreg frames=0 dump=<node power=0 pending=0>" in result.stdout
    assert "NATIVE done" in result.stdout


def test_format_registry(dut):
    dut.expect("TEST start format_registry", timeout=10)
    dut.expect(
        "values cmd=1 tel=2 again=1 vendor=3 overflow=0 got=1 telemetry=0401",
        timeout=10)
    dut.expect("01 000000 main app frame.tx bus=0 fmt=node.cmd bits=16 data=0508",
               timeout=10)
    dut.expect("02 000000 main app frame.tx bus=0 fmt=node.cmd bits=16 data=0408",
               timeout=10)
    dut.expect(
        "03 000000 main app frame.tx bus=0 fmt=vendor.cal bits=16 data=0408",
        timeout=10)
    dut.expect("04 000000 main app frame.tx bus=1 fmt=node.cmd bits=16 data=0400",
               timeout=10)
    dut.expect("05 001000 tick dev dev.frame bus=0 fmt=node.tel bits=16 data=0401",
               timeout=10)
    dut.expect("06 002000 tick diag diag.fmt_full name=overflow.x", timeout=10)
    dut.expect("07 002000 main dir dump node power=1 pending=0", timeout=10)
    dut.expect("stats events=7 dropped=0 diag=1", timeout=10)
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
