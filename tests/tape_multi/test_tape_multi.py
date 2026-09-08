"""X65: one tape per device, several devices per session.

A capture of a bus with two parts on it, of a SPI flash, and of a bus and
a serial port used at once. trace2tape.py splits each trace by source
(--addr, --spi, --serial) into one tape per device; the replay binds the
tapes where the parts were and must give the application the same bytes,
with no tape reporting a departure.
"""

import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
SRC = ROOT / "src"
MODELS = ROOT / "devices" / "src"
ENV = HERE.parent / "common_env"
SCENARIOS = HERE.parent / "common_scenarios"
TOOL = ROOT / "devices" / "tools" / "trace2tape.py"
NATIVE = HERE / "native"

TAPES = [
    # (trace, selection, name, header)
    ("two", ["--addr", "48"], "TwoTemp", "two_temp_tape.h"),
    ("two", ["--addr", "76"], "TwoEnv", "two_env_tape.h"),
    ("flash", ["--spi"], "Flash", "flash_tape.h"),
    ("mixed", ["--addr", "76"], "MixedEnv", "mixed_env_tape.h"),
    ("mixed", ["--serial"], "MixedModem", "mixed_modem_tape.h"),
]


def build(binary, sources, includes):
    subprocess.run(
        ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
         *[f"-I{i}" for i in includes], *[str(s) for s in sources],
         "-o", str(binary)],
        check=True,
    )


def run(binary):
    return subprocess.run([str(binary)], capture_output=True, text=True,
                          check=True).stdout


def split_runs(stdout):
    runs = {}
    current = None
    for line in stdout.splitlines():
        m = re.match(r"RUN (\w+) (\w+) BEGIN$", line)
        if m:
            current = (m.group(1), m.group(2))
            runs[current] = {"trace": [], "results": [], "stats": ""}
            continue
        if re.match(r"RUN \w+ \w+ END$", line):
            current = None
            continue
        if current is None:
            continue
        if line.startswith("results"):
            runs[current]["results"] = line.split()[1:]
        elif line.startswith("stats"):
            runs[current]["stats"] = line
        else:
            runs[current]["trace"].append(line)
    return runs


def device_lines(trace):
    out = []
    for line in trace:
        cols = line.split(" ", 4)
        if len(cols) < 5 or cols[3] != "dev":
            continue
        text = re.sub(r" re=\d+$", "", cols[4])
        if text.startswith(("dev.note", "dump", "gpio.inject")):
            continue
        out.append((cols[1], text))
    return out


def notes(trace):
    return [re.sub(r"^\d+ \d+ \S+ dev dev\.note ", "", l)
            for l in trace if " dev dev.note " in l]


def dumps(trace):
    return [l.split(" dir dump ", 1)[1] for l in trace if " dir dump " in l]


def test_tape_multi():
    out = HERE / "output"
    out.mkdir(exist_ok=True)

    capture = out / "capture"
    build(capture,
          [NATIVE / "capture.cpp", SCENARIOS / "scenarios.cpp", ENV / "nenv.cpp",
           MODELS / "temp_model.cpp", MODELS / "env_sensor_model.cpp",
           MODELS / "modem_model.cpp", MODELS / "unit_flash_model.cpp"],
          [SRC, MODELS, ENV, SCENARIOS])
    captured = split_runs(run(capture))
    for name in ("two", "flash", "mixed"):
        assert captured[("capture", name)]["stats"].endswith("dropped=0")
        (out / f"{name}.trace").write_text(
            "\n".join(captured[("capture", name)]["trace"]) + "\n")
    summaries = {}
    for trace, selection, name, header in TAPES:
        result = subprocess.run(
            [sys.executable, str(TOOL), str(out / f"{trace}.trace"), *selection,
             "--name", name, "--out", str(out / header)],
            capture_output=True, text=True, check=True)
        summaries[name] = result.stdout.strip()
        print(summaries[name])
    # Each tape holds its own device's part of the session and nothing else.
    assert summaries["TwoTemp"] == "TwoTemp: 5 steps (2 read, 3 write), 0 left out"
    assert summaries["TwoEnv"] == "TwoEnv: 5 steps (2 read, 3 write), 0 left out"
    # A run of single-byte transfers between chip-select edges is one step:
    # status, write-enable, status, program, status, status, read.
    assert summaries["Flash"] == "Flash: 7 steps (7 spi), 0 left out"
    assert summaries["MixedEnv"] == "MixedEnv: 7 steps (3 read, 4 write), 0 left out"
    assert summaries["MixedModem"] == (
        "MixedModem: 4 steps (2 serialin, 2 serialout), 0 left out")
    flash_header = (out / "flash_tape.h").read_text()
    # The program command: four MOSI bytes, four idle MISO bytes, as one step.
    assert ("kFlashTape_3[8] = {0x02, 0x10, 0xAB, 0xCD, 0xFF, 0xFF, 0xFF, 0xFF}"
            in flash_header)
    assert "{TapeModel::kSpi, 0, 1, 4, 0, kFlashTape_3, 0}," in flash_header

    replay = out / "replay"
    build(replay,
          [NATIVE / "replay.cpp", SCENARIOS / "scenarios.cpp", ENV / "nenv.cpp",
           MODELS / "tape_model.cpp"],
          [SRC, MODELS, ENV, SCENARIOS, out])
    replayed = split_runs(run(replay))

    for name in ("two", "flash", "mixed"):
        orig = captured[("capture", name)]
        tape = replayed[("tape", name)]
        assert tape["results"] == orig["results"], (name, orig["results"],
                                                    tape["results"])
        assert device_lines(tape["trace"]) == device_lines(orig["trace"]), name
        assert notes(tape["trace"]) == [], (name, notes(tape["trace"]))
        for d in dumps(tape["trace"]):
            assert d.startswith("tape step=") and d.endswith(" mismatch=0"), d
            steps = d.split("step=")[1].split(" ")[0]
            assert steps.split("/")[0] == steps.split("/")[1], d
    # The flash answered what it was programmed with, through the tape.
    assert captured[("capture", "flash")]["results"][-2:] == ["XAB", "XCD"]
