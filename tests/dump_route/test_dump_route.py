"""X12's last question: which route should an inspection take to become
evidence — a text dump, a channel read, or the model's own API?"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"


def test_dump_route():
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

    # The text dump reads well and carries both facts, but a test can only
    # compare the whole sentence: the fields are prose.
    assert ("route text lines=1 text=<dump sensor raw=1234 fault=1> "
            "machine_readable=0") in result.stdout

    # The channel read carries the same two facts as bytes, so a test can
    # assert on one of them without parsing the other.
    assert ("route channel lines=1 text=<dump.chan chan=0 len=3 data=04D201> "
            "machine_readable=1 value=1234 fault=1") in result.stdout

    # The model's own API is invisible to the environment: whatever the
    # director records is its own invention, and it may record nothing.
    assert ("route direct lines=1 text=<note raw=1234> environment_can_see=0"
            in result.stdout)

    # None of the routes costs more than the others in calls or events, so
    # the choice is about what the evidence carries, not about its price.
    assert ("cost text_calls=1 channel_calls=1 direct_calls=1 text_fields=0 "
            "channel_fields=3") in result.stdout
    assert "NATIVE done" in result.stdout
