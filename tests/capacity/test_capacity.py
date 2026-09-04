"""Size limits negotiate per environment; frames stay atomic; oversize is
rejected visibly."""

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
    # 64-bit port: 5 segments of 8 bytes ([index,total] + 6 data); the
    # atomic 128-bit snapshot is refused, not split.
    assert ("small frames=5 refused=1 bytes=40 max_chunk=8 sum=B6 "
            "dump=<bulk sent=5 unsent=1>") in result.stdout
    # 4096-bit port: one 32-byte segment plus the 16-byte snapshot.
    assert ("large frames=2 refused=0 bytes=48 max_chunk=32 sum=0C "
            "dump=<bulk sent=2 unsent=0>") in result.stdout
    # No frame routing: format ids are 0, nothing is attempted.
    assert "noroute frames=0 dump=<bulk sent=0 unsent=0>" in result.stdout
    assert "NATIVE done" in result.stdout


def test_capacity(dut):
    dut.expect("TEST start capacity", timeout=10)
    dut.expect("values capacity=64 device_calls=0", timeout=10)
    dut.expect("01 000000 main dir chan.write chan=0 data=10", timeout=10)
    dut.expect("02 000000 main dir chan.write chan=1 data=01", timeout=10)
    # Five complete "acme.bulk.1" frames, summarized as length + checksum.
    dut.expect("03 000000 main dev dev.frame bus=0 fmt=acme.bulk.1 bits=64 "
               "len=8 sum=74", timeout=10)
    dut.expect("04 000000 main dev dev.frame bus=0 fmt=acme.bulk.1 bits=64 "
               "len=8 sum=99", timeout=10)
    dut.expect("05 000000 main dev dev.frame bus=0 fmt=acme.bulk.1 bits=64 "
               "len=8 sum=BE", timeout=10)
    dut.expect("06 000000 main dev dev.frame bus=0 fmt=acme.bulk.1 bits=64 "
               "len=8 sum=E3", timeout=10)
    dut.expect("07 000000 main dev dev.frame bus=0 fmt=acme.bulk.1 bits=64 "
               "len=8 sum=08", timeout=10)
    # The atomic snapshot does not fit: refused with a diagnostic, unsent.
    dut.expect("08 000000 main dir chan.write chan=2 data=01", timeout=10)
    dut.expect("09 000000 main diag diag.frame_oversize bus=0 bits=128 max=64",
               timeout=10)
    # An application frame over the limit is refused the same way.
    dut.expect("10 000000 main diag diag.frame_oversize bus=0 bits=128 max=64",
               timeout=10)
    dut.expect("11 000000 main dir dump bulk sent=5 unsent=1", timeout=10)
    dut.expect("stats events=11 dropped=0 diag=2", timeout=10)
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
