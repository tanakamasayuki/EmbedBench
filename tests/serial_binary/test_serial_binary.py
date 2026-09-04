"""serialOut carries any byte: a NUL survives queue, log, and application."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
MODELS = HERE.parent / "common_models" / "src"
ENV = HERE.parent / "common_env"


def test_native_binary():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{MODELS}", f"-I{ENV}",
            str(HERE / "native" / "main.cpp"),
            str(ENV / "nenv.cpp"),
            str(MODELS / "modem_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # All three bytes reach the application, NUL included.
    assert "binary got=3 bytes=410042" in result.stdout
    # The log records them as hex rather than stopping at the NUL, and each
    # consumed byte is escaped where it is not printable.
    assert "01 000000 main app uart.tx AT+B;" in result.stdout
    assert "02 000000 main dev dev.tx data=410042" in result.stdout
    assert "03 000000 main app uart.rx A" in result.stdout
    assert "04 000000 main app uart.rx 0x00" in result.stdout
    assert "05 000000 main app uart.rx B" in result.stdout
    assert "NATIVE done" in result.stdout


def test_serial_binary(dut):
    dut.expect("TEST start serial_binary", timeout=10)
    dut.expect("values got=3 bytes=410042", timeout=10)
    dut.expect(r"01 000000 main app uart.tx AT\+B;", timeout=10)
    dut.expect("02 000000 main dev dev.tx data=410042", timeout=10)
    dut.expect("03 000000 main app uart.rx A", timeout=10)
    dut.expect("04 000000 main app uart.rx 0x00", timeout=10)
    dut.expect("05 000000 main app uart.rx B", timeout=10)
    dut.expect("06 000000 main dir dump modem replies=1 pending=0", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
