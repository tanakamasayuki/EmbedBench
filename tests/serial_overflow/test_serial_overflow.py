"""serialOut when the receive queue cannot take the whole reply."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
ENV = HERE.parent / "common_env"


def test_native_overflow():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{ENV}", f"-I{HERE}",
            str(HERE / "native" / "main.cpp"),
            str(ENV / "nenv.cpp"),
            str(HERE / "flood_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # The accepted prefix arrives, the suffix does not, and the device saw
    # the refusal in serialOut's return value (refused=1).
    assert ("overflow read=8 prefix=01234567 leftover=0 "
            "dump=<flood sent=1 refused=1>") in result.stdout
    assert "01 000000 main app uart.tx go" in result.stdout
    assert "02 000000 main dev dev.tx 0123456789AB" in result.stdout
    # Exactly one diagnostic naming what was accepted and what was offered.
    assert "03 000000 main diag diag.uart_rx_full accepted=8 len=12" \
        in result.stdout
    assert result.stdout.count("diag.uart_rx_full") == 1
    assert "04 000000 main app uart.rx 0" in result.stdout
    assert "11 000000 main app uart.rx 7" in result.stdout
    # Nothing beyond the eight accepted bytes was ever recorded as received.
    assert "uart.rx 8" not in result.stdout
    assert "NATIVE done" in result.stdout


def test_serial_overflow(dut):
    dut.expect("TEST start serial_overflow", timeout=10)
    dut.expect("values read=8 prefix=01234567 leftover=0", timeout=10)
    dut.expect("01 000000 main app uart.tx go", timeout=10)
    dut.expect("02 000000 main dev dev.tx 0123456789AB", timeout=10)
    dut.expect("03 000000 main diag diag.uart_rx_full accepted=8 len=12",
               timeout=10)
    dut.expect("04 000000 main app uart.rx 0", timeout=10)
    dut.expect("11 000000 main app uart.rx 7", timeout=10)
    dut.expect("12 000000 main dir dump flood sent=1 refused=1", timeout=10)
    # One diagnostic, and no partial delivery went unrecorded.
    dut.expect("stats events=12 diag=1", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
