"""X63: a capture as the scaffold of a device model.

Three catalog models are driven through their known sequences on
environment example #2 and their traces taken as if captured on a board.
devices/tools/trace2regtable.py turns each trace into a RegTableModel
table plus a TODO list of what it could not infer; the same sequences
then run against the table alone, and against the table with the hooks a
person wrote from that TODO list (native/hooks.h).

What is compared is what the application saw — every status and every
byte, through the results line — because the trace itself keeps at most
five payload bytes: the IMU's 16-byte burst reads identically in the log
whether or not bytes 6..16 are right. Lines the device moved are
compared from the trace, where they are complete.
"""

import importlib.util
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
SRC = ROOT / "src"
MODELS = ROOT / "devices" / "src"
ENV = HERE.parent / "common_env"
TOOL = ROOT / "devices" / "tools" / "trace2regtable.py"
NATIVE = HERE / "native"

SCENARIOS = ["temp", "env", "imu"]
ORIGINALS = {
    "temp": "temp_model",
    "env": "env_sensor_model",
    "imu": "unit_imu_model",
}
LINE_RE = re.compile(r"^\d+ (\d+) \S+ dev gpio\.inject line=(\d+) val=(\d+)$")


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
    """{(variant, name): {"trace": [...], "results": [...], "stats": str}}"""
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


def lines_moved(trace):
    out = []
    for line in trace:
        m = LINE_RE.match(line)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3)))
    return out


def notes(trace):
    return sum(1 for line in trace if " dev dev.note " in line)


def compare(original, replay):
    assert len(original["results"]) == len(replay["results"]), (
        original["results"], replay["results"])
    matched = sum(1 for a, b in zip(original["results"], replay["results"])
                  if a == b)
    want = lines_moved(original["trace"])
    have = list(lines_moved(replay["trace"]))
    hit = 0
    for item in want:
        if item in have:
            have.remove(item)
            hit += 1
    return {
        "requests": f"{matched}/{len(original['results'])}",
        "lines": f"{hit}/{len(want)}",
        "spurious_lines": len(have),
        "notes": f"{notes(replay['trace'])}/{notes(original['trace'])}",
    }


def loc(path: Path) -> int:
    return len([
        l for l in path.read_text().splitlines()
        if l.strip() and not l.strip().startswith("//")
    ])


def region_loc(path: Path, name: str) -> int:
    match = re.search(
        rf"// \[{re.escape(name)} begin\]\n(.*?)// \[{re.escape(name)} end\]",
        path.read_text(), re.DOTALL)
    assert match, f"missing markers for {name}"
    return len([
        l for l in match.group(1).splitlines()
        if l.strip() and not l.strip().startswith("//")
    ])


def load_tool():
    spec = importlib.util.spec_from_file_location("trace2regtable", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_capture_scaffold():
    out = HERE / "output"
    out.mkdir(exist_ok=True)

    # 1. Capture: the hand-written models, as if on a board.
    capture = out / "capture"
    build(capture,
          [NATIVE / "capture.cpp", NATIVE / "scenarios.cpp", ENV / "nenv.cpp",
           *[MODELS / f"{ORIGINALS[n]}.cpp" for n in SCENARIOS]],
          [SRC, MODELS, ENV])
    captured = split_runs(run(capture))
    for name in SCENARIOS:
        (out / f"{name}.trace").write_text(
            "\n".join(captured[("capture", name)]["trace"]) + "\n")
        assert captured[("capture", name)]["stats"].endswith("dropped=0")

    # 2. Convert: one table + TODO list per trace.
    todo = {}
    for name in SCENARIOS:
        header = out / f"{name}_table.h"
        result = subprocess.run(
            [sys.executable, str(TOOL), str(out / f"{name}.trace"),
             "--name", name.capitalize(), "--out", str(header),
             "--report", str(out / f"{name}_report.txt")],
            capture_output=True, text=True, check=True)
        print(result.stdout.strip())
        todo[name] = len([l for l in header.read_text().splitlines()
                          if l.startswith("//   - ")])

    # 3. Replay: the table alone, then the table with hand-written hooks.
    replay = out / "replay"
    build(replay,
          [NATIVE / "replay.cpp", NATIVE / "scenarios.cpp", ENV / "nenv.cpp",
           MODELS / "regtable_model.cpp"],
          [SRC, MODELS, ENV, out, NATIVE])
    replayed = split_runs(run(replay))

    coverage = {}
    for name in SCENARIOS:
        for variant in ("table", "hooked"):
            coverage[(variant, name)] = compare(captured[("capture", name)],
                                                replayed[(variant, name)])
    sizes = {name: region_loc(NATIVE / "hooks.h", name) for name in SCENARIOS}
    originals = {name: loc(MODELS / f"{ORIGINALS[name]}.h") +
                 loc(MODELS / f"{ORIGINALS[name]}.cpp") for name in SCENARIOS}
    for name in SCENARIOS:
        print(f"{name}: todo={todo[name]} table={coverage[('table', name)]} "
              f"hooked={coverage[('hooked', name)]} hook_loc={sizes[name]} "
              f"original_loc={originals[name]}")

    # The table alone answers everything that is constant: every write
    # status, every read whose contents never changed. What it misses is
    # exactly what the TODO list names — a status that cleared with time,
    # a FIFO whose bytes and length are state, and every line.
    assert coverage[("table", "temp")] == {
        "requests": "3/3", "lines": "0/1", "spurious_lines": 0, "notes": "0/0"}
    assert coverage[("table", "env")] == {
        "requests": "10/11", "lines": "0/1", "spurious_lines": 0, "notes": "1/1"}
    assert coverage[("table", "imu")] == {
        "requests": "6/8", "lines": "0/3", "spurious_lines": 0, "notes": "0/1"}
    assert todo == {"temp": 1, "env": 4, "imu": 8}
    # The IMU's burst read is the case the trace cannot see: 16 bytes came
    # back in both runs and the log shows the same five, but the table
    # answered zeros past the fifth byte.
    orig_burst = captured[("capture", "imu")]["results"][4]
    table_burst = replayed[("table", "imu")]["results"][4]
    assert orig_burst == "R" + "1234" * 8
    assert table_burst == "R" + "1234123412" + "00" * 11
    assert any("data=1234123412" in l for l in replayed[("table", "imu")]["trace"])

    # With the hooks written from the TODO list, the application sees the
    # same bytes and the same lines as against the hand-written model.
    for name in SCENARIOS:
        c = coverage[("hooked", name)]
        assert c["requests"].split("/")[0] == c["requests"].split("/")[1], c
        assert c["lines"].split("/")[0] == c["lines"].split("/")[1], c
        assert c["spurious_lines"] == 0, c
        assert c["notes"].split("/")[0] == c["notes"].split("/")[1], c

    # The hook is the part a person still writes. Pinned so that a change
    # in what the table can carry shows up here as a deliberate act; the
    # originals' sizes are printed for the ledger, not pinned (FACTS).
    assert sizes == {"temp": 8, "env": 40, "imu": 78}


HOST_SAMPLE = """\
02 000000 main app i2c.req addr=76 data=D0 stop=0
03 000000 main dev i2c.resp status=0 re=2
04 000000 main app i2c.rd.req addr=76 req=1 stop=1 rs
05 000000 main dev i2c.rd.resp len=1 data=60 re=4
06 000000 main app i2c.req addr=76 data=F425 stop=1
07 000000 main dev i2c.resp status=0 re=6
08 000000 main app i2c.req addr=76 data=F3 stop=0 x8..007000
09 000000 main dev i2c.resp status=0 re=8 x8..007000
10 000000 main app i2c.rd.req addr=76 req=1 stop=1 rs x8..007000
11 000000 main dev i2c.rd.resp len=1 data=08 re=10 x8..007000
40 007500 tick dev gpio.inject pin=27 0->1 match=0
41 008500 main app i2c.req addr=76 data=F3 stop=0
42 008500 main dev i2c.resp status=0 re=41
43 008500 main app i2c.rd.req addr=76 req=1 stop=1 rs
44 008500 main dev i2c.rd.resp len=1 data=00 re=43
"""

SHIM_SAMPLE = """\
123456 i2c.req addr=76 data=D0 stop=0
123500 i2c.resp status=0
123600 i2c.rd.req addr=76 req=1 stop=1 rs
123700 i2c.rd.resp len=1 data=60
"""


def test_parser_accepts_host_and_shim_lines():
    """The host environment's lines (folds, pin edges) and a bare
    timestamp-plus-event line a board-side shim would print both parse
    into the same analysis."""
    tool = load_tool()
    a = tool.analyze(tool.parse(HOST_SAMPLE.splitlines()), 0x76)
    assert sorted(a.regs) == [0xD0, 0xF3, 0xF4]
    assert a.regs[0xD0].reset == b"\x60"
    # A folded round is one observation, not eight.
    assert [o.hex() for o in a.regs[0xF3].obs] == ["08", "00"]
    assert a.regs[0xF3].volatile
    assert a.regs[0xF4].writable and a.regs[0xF4].reset == b"\x00"
    assert a.require_rs
    assert a.lines == [(7500, "27", "1",
                        "7500 us after write F4=25, 7500 us after read F3 "
                        "(on the device's own schedule)")]

    b = tool.analyze(tool.parse(SHIM_SAMPLE.splitlines()), 0x76)
    assert sorted(b.regs) == [0xD0]
    assert b.regs[0xD0].reset == b"\x60"
    assert b.regs[0xD0].obs[0].time == 123600
