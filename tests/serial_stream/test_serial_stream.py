"""Serial byte stream: call boundaries carry no meaning."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
MODELS = HERE.parent / "common_models" / "src"


def test_native_stream():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{MODELS}",
            str(HERE / "native" / "main.cpp"),
            str(MODELS / "modem_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert "stream whole=1 bytes=1 chunks=1 two_in_one=2" in result.stdout
    assert "NATIVE done" in result.stdout


def test_serial_stream(dut):
    dut.expect("TEST start serial_stream", timeout=10)
    dut.expect("values whole=OK e1=1000 split=OK e2=1000", timeout=10)
    dut.expect(r"01 000000 main app uart.tx AT\+S;", timeout=10)
    dut.expect("02 001000 tick dev dev.tx OK", timeout=10)
    dut.expect("03 001000 main app uart.rx O", timeout=10)
    dut.expect("04 001000 main app uart.rx K", timeout=10)
    # Five single-byte writes are five records of what the app did...
    dut.expect("05 001000 main app uart.tx A", timeout=10)
    dut.expect("06 001000 main app uart.tx T", timeout=10)
    dut.expect(r"07 001000 main app uart.tx \+", timeout=10)
    dut.expect("08 001000 main app uart.tx S", timeout=10)
    dut.expect("09 001000 main app uart.tx ;", timeout=10)
    # ...and the device reply is identical: same bytes, same latency.
    dut.expect("10 002000 tick dev dev.tx OK", timeout=10)
    dut.expect("11 002000 main app uart.rx O", timeout=10)
    dut.expect("12 002000 main app uart.rx K", timeout=10)
    dut.expect("13 002000 main dir dump modem replies=2 pending=0", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
