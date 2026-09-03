"""Size limits negotiate per environment; oversize is rejected visibly."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "bulk_model.h", HERE / "bulk_model.cpp"]:
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
            str(HERE / "bulk_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # The same model splits per the negotiated capacity: 4 frames on the
    # 64-bit port, 1 frame on the 4096-bit port, same payload checksum.
    assert ("small frames=4 bytes=32 max_chunk=8 sum=F0 dump=<bulk sent=4>"
            in result.stdout)
    assert ("large frames=1 bytes=32 max_chunk=32 sum=F0 dump=<bulk sent=1>"
            in result.stdout)
    assert "noroute frames=0 dump=<bulk sent=0>" in result.stdout
    assert "NATIVE done" in result.stdout


def test_capacity(dut):
    dut.expect("TEST start capacity", timeout=10)
    dut.expect("values capacity=64 device_calls=0", timeout=10)
    dut.expect("01 000000 main dir chan.write chan=0 data=10", timeout=10)
    dut.expect("02 000000 main dir chan.write chan=1 data=01", timeout=10)
    # Bulk payloads are summarized (length + checksum) per SCOPE 3.2.
    dut.expect("03 000000 main dev dev.frame bus=0 fmt=bulk.data bits=64 "
               "len=8 sum=9C", timeout=10)
    dut.expect("04 000000 main dev dev.frame bus=0 fmt=bulk.data bits=64 "
               "len=8 sum=DC", timeout=10)
    dut.expect("05 000000 main dev dev.frame bus=0 fmt=bulk.data bits=64 "
               "len=8 sum=1C", timeout=10)
    dut.expect("06 000000 main dev dev.frame bus=0 fmt=bulk.data bits=64 "
               "len=8 sum=5C", timeout=10)
    dut.expect("07 000000 main diag diag.frame_oversize bus=0 bits=128 max=64",
               timeout=10)
    dut.expect("08 000000 main dir dump bulk sent=4", timeout=10)
    dut.expect("stats events=8 dropped=0 diag=1", timeout=10)
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
