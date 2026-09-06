"""WP-B2 remainder: parse time and diff readability of the three formats."""

import difflib
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).parent
EVENTS = 100000


def test_log_formats():
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
    result = subprocess.run([str(binary), str(out_dir)], capture_output=True,
                            text=True, check=True)

    measured = {}
    for name, size, write_us, parse_us, parsed in re.findall(
            r"format (\w+) bytes=(\d+) write_us=(\d+) parse_us=(\d+) "
            r"parsed=(\d+)", result.stdout):
        measured[name] = {
            "bytes": int(size),
            "write_us": int(write_us),
            "parse_us": int(parse_us),
            "parsed": int(parsed),
        }
    assert set(measured) == {"seq_first", "time_first", "json"}

    # Every line parses back, in every format.
    for name, values in measured.items():
        assert values["parsed"] == EVENTS, (name, values)

    # Fixed-width lines are exactly 60 bytes (59 plus newline) whatever the
    # values hold, so the file size is known in advance from the event
    # count alone.
    assert measured["seq_first"]["bytes"] == EVENTS * 60
    assert measured["time_first"]["bytes"] == EVENTS * 60
    # JSON lines grow and shrink with the digits in them, so its size is a
    # function of the data, not just the event count: here it averages just
    # under 100 bytes for the same events, about 1.66x the fixed width.
    assert 9_900_000 < measured["json"]["bytes"] < 10_000_000
    ratio = measured["json"]["bytes"] / measured["seq_first"]["bytes"]
    assert 1.6 < ratio < 1.7

    # Timings are recorded but deliberately not asserted on. Two attempts
    # to pin a relationship between them failed on different runs — first
    # an ordering, then a factor-of-two band — because the numbers move
    # with whatever else the machine is doing. What they showed each time
    # is that parse time does not separate the formats, so the choice
    # rests on the deterministic properties above.

    # Diff readability: one changed field in the middle of the stream.
    diffs = {}
    for name in measured:
        base = (out_dir / f"{name}.log").read_text().splitlines()
        variant = (out_dir / f"{name}.variant.log").read_text().splitlines()
        changed = [
            line for line in difflib.unified_diff(base, variant, n=0)
            if line.startswith(("+", "-")) and not line.startswith(("+++", "---"))
        ]
        diffs[name] = changed
    print(f"MEASURED {measured}")
    print(f"DIFF {{k: len(v) for k, v in diffs.items()}} -> "
          f"{ {k: len(v) for k, v in diffs.items()} }")

    # All three isolate the change to one line pair: the line format does
    # not affect how much a diff shows, only how much there is to read.
    for name, changed in diffs.items():
        assert len(changed) == 2, (name, changed)

    # What differs is readability: the fixed-width line puts the sequence
    # first, so the changed line identifies itself in its first field.
    removed = diffs["seq_first"][0]
    assert removed.startswith("-00050001 "), removed
    # The JSON line carries the same information behind 60 characters of
    # field names.
    assert diffs["json"][0].startswith('-{"seq":50001,'), diffs["json"][0]
