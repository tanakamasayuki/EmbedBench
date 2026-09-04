"""An over-long i2cRead() result is diagnosed, never trusted."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
ENV = HERE.parent / "common_env"


def test_native_badlen():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{ENV}", f"-I{HERE}",
            str(HERE / "native" / "main.cpp"),
            str(ENV / "nenv.cpp"),
            str(HERE / "badlen_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # The model wrote its two bytes but claimed three: the environment
    # reports nothing supplied rather than describing a third byte.
    assert "badlen got=0 buf=A0A1" in result.stdout
    assert "01 000000 main app i2c.rd.req addr=60 req=2 stop=1" in result.stdout
    assert "02 000000 main diag diag.i2c_read_length addr=60 got=3 max=2" \
        in result.stdout
    assert "03 000000 main dev i2c.rd.resp len=0 data=" in result.stdout
    assert "NATIVE done" in result.stdout


def test_i2c_badlen(dut):
    dut.expect("TEST start i2c_badlen", timeout=10)
    dut.expect("values got=0 diag=1", timeout=10)
    dut.expect("01 000000 main app i2c.rd.req addr=60 req=2 stop=1", timeout=10)
    dut.expect("02 000000 main diag diag.i2c_read_length addr=60 got=3 max=2",
               timeout=10)
    dut.expect("03 000000 main dev i2c.rd.resp len=0 data=", timeout=10)
    dut.expect("04 000000 main dir dump badlen reads=1", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
