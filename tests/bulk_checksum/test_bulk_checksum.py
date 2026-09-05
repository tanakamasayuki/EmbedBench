"""X28: what a bulk summary must carry to still catch a difference."""

import re
import subprocess
from pathlib import Path

HERE = Path(__file__).parent


def test_bulk_checksum():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
            str(HERE / "native" / "main.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    scores = {
        name: (int(cases), int(s), int(c))
        for name, cases, s, c in re.findall(
            r"case (\w+) cases=(\d+) sum_caught=(\d+) crc_caught=(\d+)",
            result.stdout)
    }
    print(f"SCORES {scores}")
    assert set(scores) == {"changed_byte", "swapped_pair", "lost_byte",
                           "duplicated_byte"}

    # A changed byte moves the sum too, so both notice it.
    cases, sum_caught, crc_caught = scores["changed_byte"]
    assert sum_caught == cases and crc_caught == cases

    # Two swapped bytes leave the sum identical by construction: a sum can
    # never see a reordering, and a framebuffer-style payload is full of
    # them. CRC-8 sees all but its own 1-in-256 collisions.
    cases, sum_caught, crc_caught = scores["swapped_pair"]
    assert sum_caught == 0
    assert crc_caught > cases * 0.98, crc_caught

    # Losing or duplicating a byte (with the payload shifted, so the length
    # stays the same) is caught by both above 99% of the time, and neither
    # is clearly better: this is not where the choice is decided. In a real
    # record the count is a field of its own, so a changed length is caught
    # exactly rather than by luck.
    for name in ("lost_byte", "duplicated_byte"):
        cases, sum_caught, crc_caught = scores[name]
        assert sum_caught > cases * 0.99, (name, sum_caught)
        assert crc_caught > cases * 0.99, (name, crc_caught)
    assert "NATIVE done" in result.stdout
