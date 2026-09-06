"""Evidence for three PROPOSED interface additions, each measured against
the workaround it would replace. The paths live in proposed_port.h, not in
the frozen header: this is the material for an approval request, and the
interface does not move until the maintainer says so."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
ENV = HERE.parent / "common_env"


def test_if_gaps():
    # The proposal is deliberately outside src/: the frozen surface is
    # unchanged while the evidence is gathered.
    assert "analogOut" not in (SRC / "embedbench_device.h").read_text()
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}",
            str(HERE / "native" / "main.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert "revision version=1 revision=001" in result.stdout

    # analogOut (proposed): without it the value cannot leave the device on its own —
    # the application reads 0 until the director pulls it out by hand, one
    # step per update. With it the device presents its own voltage and the
    # director does nothing.
    assert ("analog without_routing=0 after_pull=1234 director_steps=1 "
            "refused=1 | with_routing=1234 director_steps=0 pushed=1"
            ) in result.stdout

    # requestWake (proposed): a 1500 us latency in a 1000 us tick is served at 2000 us
    # without the request — 500 us late, and the error is a property of the
    # environment's tick rather than of the device. With it the reply lands
    # exactly when it is due.
    assert ("wake due=1500 without_routing_at=2000 accepted=0 | "
            "with_routing_at=1500 accepted=1") in result.stdout

    # diagnose (proposed): a protocol error on a port with no return value is counted
    # inside the model and invisible to the log until someone dumps it.
    assert ("diagnose without_routing errors=1 reported=0 recorded=0 | "
            "with_routing errors=1 reported=1 recorded=1 "
            "last=byte outside frame") in result.stdout
    assert "NATIVE done" in result.stdout
