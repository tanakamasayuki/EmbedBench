"""Why revisions 002-004 exist: each addition measured against the
workaround it replaced, in the same environment, so the comparison is about
the interface rather than about two different programs."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
ENV = HERE.parent / "common_env"


def test_if_gaps():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{ENV}",
            str(HERE / "native" / "main.cpp"),
            str(ENV / "nenv.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert "revision version=1 revision=004" in result.stdout

    # analogOut (revision 002): without it the value cannot leave the device on its own —
    # the application reads 0 until the director pulls it out by hand, one
    # step per update. With it the device presents its own voltage and the
    # director does nothing.
    assert ("analog without_routing=0 after_pull=1234 director_steps=1 "
            "refused=1 | with_routing=1234 director_steps=0 pushed=1"
            ) in result.stdout

    # requestWake (revision 003): a 1500 us latency in a 1000 us tick is served at 2000 us
    # without the request — 500 us late, and the error is a property of the
    # environment's tick rather than of the device. With it the reply lands
    # exactly when it is due.
    assert ("wake due=1500 without_routing_at=2000 accepted=0 | "
            "with_routing_at=1500 accepted=1") in result.stdout

    # diagnose (revision 004): a protocol error on a port with no return value is counted
    # inside the model and invisible to the log until someone dumps it.
    assert ("diagnose without_routing errors=1 reported=0 recorded=0 | "
            "with_routing errors=1 reported=1 recorded=1 "
            "last=byte outside frame") in result.stdout
    # The same three paths behave the same on environment example #2, so
    # this is a property of the interface rather than of one purpose-built
    # environment: the voltage arrives without a director step, the reply
    # lands at 1500 us rather than the 2000 us tick, and the note is kept.
    assert "shared analog=1234 replied_at=1500 wake=1 notes=1" in result.stdout
    assert "NATIVE done" in result.stdout
