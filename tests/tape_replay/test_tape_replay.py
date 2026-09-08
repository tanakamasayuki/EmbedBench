"""X64: a capture played back as a tape.

The environmental sensor (I2C) and the AT modem (serial) are captured on
environment example #2; devices/tools/trace2tape.py turns each trace into
a TapeModel tape; the same sequences run against the tapes and must give
the application the same bytes at the same times with no diagnostic.
Then an application that strays from the recording runs against the
sensor's tape, and the tape has to say where.
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
    """Device-visible events with the sequence numbers stripped: what the
    application received, and when."""
    out = []
    for line in trace:
        cols = line.split(" ", 4)
        if len(cols) < 5 or cols[3] != "dev":
            continue
        text = re.sub(r" re=\d+$", "", cols[4])
        if text.startswith("dev.note") or text.startswith("dump"):
            continue
        out.append((cols[1], text))
    return out


def notes(trace):
    return [re.sub(r"^\d+ \d+ \S+ dev dev\.note ", "", l)
            for l in trace if " dev dev.note " in l]


def test_tape_replay():
    out = HERE / "output"
    out.mkdir(exist_ok=True)

    capture = out / "capture"
    build(capture,
          [NATIVE / "capture.cpp", SCENARIOS / "scenarios.cpp", ENV / "nenv.cpp",
           MODELS / "env_sensor_model.cpp", MODELS / "modem_model.cpp"],
          [SRC, MODELS, ENV, SCENARIOS])
    captured = split_runs(run(capture))
    for name in ("env", "modem"):
        (out / f"{name}.trace").write_text(
            "\n".join(captured[("capture", name)]["trace"]) + "\n")
        result = subprocess.run(
            [sys.executable, str(TOOL), str(out / f"{name}.trace"),
             "--name", name.capitalize(), "--out", str(out / f"{name}_tape.h")],
            capture_output=True, text=True, check=True)
        print(result.stdout.strip())
    env_header = (out / "env_tape.h").read_text()
    modem_header = (out / "modem_tape.h").read_text()
    # The sensor's tape is the eleven transfers of the sequence; the read
    # that answered nothing is a step with no data.
    assert "static const TapeStep kEnvTapeSteps[11]" in env_header
    assert "{TapeModel::kRead, 0, 1, 0, 1, nullptr, 0}," in env_header
    # The modem's: two commands in, two replies out, each a tick later.
    assert "static const TapeStep kModemTapeSteps[4]" in modem_header
    assert re.search(r"\{TapeModel::kSerialOut, 0, 1, 2, 0, kModemTape_1, 1000\}",
                     modem_header)

    replay = out / "replay"
    build(replay,
          [NATIVE / "replay.cpp", SCENARIOS / "scenarios.cpp", ENV / "nenv.cpp",
           MODELS / "tape_model.cpp"],
          [SRC, MODELS, ENV, SCENARIOS, out])
    replayed = split_runs(run(replay))

    # Under the recorded sequence the tape is indistinguishable from the
    # part on the bus: same results, same device events at the same times
    # (the sensor's DRDY line excepted — a tape has no lines), no diagnostic.
    steps = {"env": 11, "modem": 4}
    for name in ("env", "modem"):
        orig = captured[("capture", name)]
        tape = replayed[("tape", name)]
        assert tape["results"] == orig["results"], (name, orig["results"],
                                                    tape["results"])
        want = [e for e in device_lines(orig["trace"])
                if not e[1].startswith("gpio.inject")]
        assert device_lines(tape["trace"]) == want, name
        assert notes(tape["trace"]) == [], notes(tape["trace"])
        assert any(l.endswith(f"dump tape step={steps[name]}/{steps[name]} "
                              "mismatch=0")
                   for l in tape["trace"]), tape["trace"][-1]

    # Off the path, the tape names each departure and keeps playing: the
    # changed command, the register read where a poll was recorded, the
    # longer read. The application still gets an answer each time.
    strayed = replayed[("strayed", "env")]
    assert notes(strayed["trace"]) == [
        "tape 2: want write F425, got F426",
        "tape 3: want write F3, got FA",
        "tape 4: read of 3, recorded 1",
    ]
    assert strayed["results"] == ["W0", "R60", "W0", "W0", "R08"]
    assert any(l.endswith("dump tape step=5/11 mismatch=3")
               for l in strayed["trace"])
