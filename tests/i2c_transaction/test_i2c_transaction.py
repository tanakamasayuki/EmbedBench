"""I2C transaction context: repeated start vs standalone read."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "regmap_model.h", HERE / "regmap_model.cpp"]:
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
            str(HERE / "regmap_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert ("regmap rs_len=1 rs_val=5A plain_len=0 after_len=1 after_val=77 "
            "dump=<regmap ptr=1 r1=77 rs=2 plain=1>") in result.stdout
    assert "NATIVE done" in result.stdout


def test_i2c_transaction(dut):
    dut.expect("TEST start i2c_transaction", timeout=10)
    dut.expect("values rs_len=1 rs_val=5A plain_len=0", timeout=10)
    # Write without STOP, read under repeated start ("rs"): data.
    dut.expect("01 000000 main app i2c.req addr=50 data=01 stop=0", timeout=10)
    dut.expect("02 000000 main dev i2c.resp status=0 re=1", timeout=10)
    dut.expect("03 000000 main app i2c.rd.req addr=50 req=1 stop=1 rs",
               timeout=10)
    dut.expect("04 000000 main dev i2c.rd.resp len=1 data=5A re=3", timeout=10)
    # Write with STOP, then a standalone read: refused by the device.
    dut.expect("05 000000 main app i2c.req addr=50 data=01 stop=1", timeout=10)
    dut.expect("06 000000 main dev i2c.resp status=0 re=5", timeout=10)
    dut.expect("07 000000 main app i2c.rd.req addr=50 req=1 stop=1", timeout=10)
    dut.expect("08 000000 main dev i2c.rd.resp len=0 data= re=7", timeout=10)
    dut.expect("09 000000 main dir dump regmap ptr=1 r1=5A rs=1 plain=1",
               timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
