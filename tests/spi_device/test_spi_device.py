"""Composite SPI device on the fixed interface: native and host runs."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_native_portability():
    for source in [HERE / "display_model.h", HERE / "display_model.cpp"]:
        includes = [
            l for l in source.read_text().splitlines()
            if l.strip().startswith("#include")
        ]
        for token in ["Arduino.h", "Host", "SPI.h", "embedbench_draft"]:
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
            str(HERE / "display_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert "data ack=00 sums=10,30 dump=<disp cmd=2C n=2 sum=30 busy=0>" \
        in result.stdout
    assert ("busy raise=0:1 calls_999=1 calls_1000=2 level=0 "
            "dump=<disp cmd=FF n=0 sum=00 busy=0>") in result.stdout
    assert "NATIVE done" in result.stdout


def test_spi_device(dut):
    dut.expect("TEST start spi_device", timeout=10)
    dut.expect("values ack=00 s1=10 s2=30 busy_reads=2", timeout=10)

    dut.expect("01 000000 main app gpio.write pin=4 val=0", timeout=10)
    dut.expect("02 000000 main app spi.req mosi=2C", timeout=10)
    dut.expect("03 000000 main dev spi.resp miso=00 re=2", timeout=10)
    dut.expect("04 000000 main app gpio.write pin=4 val=1", timeout=10)
    dut.expect("05 000000 main app spi.req mosi=10", timeout=10)
    dut.expect("06 000000 main dev spi.resp miso=10 re=5", timeout=10)
    dut.expect("07 000000 main app spi.req mosi=20", timeout=10)
    dut.expect("08 000000 main dev spi.resp miso=30 re=7", timeout=10)
    dut.expect("09 000000 main app gpio.write pin=4 val=0", timeout=10)
    # The busy line rises between the refresh byte's request and response
    # — the X20 nested case occurring in a real composite device.
    dut.expect("10 000000 main app spi.req mosi=FF", timeout=10)
    dut.expect("11 000000 main dev gpio.inject pin=26 0->1 match=0",
               timeout=10)
    dut.expect("12 000000 main dev spi.resp miso=00 re=10", timeout=10)
    dut.expect("13 000000 main app gpio.read pin=26 val=1", timeout=10)
    dut.expect("14 001000 tick dev gpio.inject pin=26 1->0 match=0",
               timeout=10)
    dut.expect("15 001000 main app gpio.read pin=26 val=0", timeout=10)
    dut.expect("16 001000 main dir dump disp cmd=FF n=0 sum=00 busy=0",
               timeout=10)

    dut.expect("stats events=16 dropped=0 resp_lines=4 diag=0", timeout=10)
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
