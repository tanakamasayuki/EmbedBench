"""devices/tools/knowledge_table.py: EmbedBenchDeviceData's knowledge as a
RegTableSpec table.

The converter is checked on a small knowledge file of its own. With a clone
of EmbedBenchDeviceData named by EMBEDBENCH_DEVICEDATA, the table it writes
for QMP6988 is also compared with the one the hand-written model carries,
so a change on either side shows up here.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
TOOL = ROOT / "devices" / "tools" / "knowledge_table.py"
MODEL = ROOT / "devices" / "src" / "qmp6988_model.cpp"

KNOWLEDGE = """\
schema_version: 1
chip: demo
addressing:
  repeated_start_required: true
registers:
  - {addr: "0x10", name: ID, width: 1, access: r, reset: "5a"}
  - {addr: "0x20", name: CTRL, width: 2, access: rw, reset: "0102"}
  - {addr: "0x30", name: DATA, width: 1, access: r, reset: "00", volatile: true}
"""

ENTRY = re.compile(r"\{0x([0-9A-F]{2}), (\d+), k\w+?_?([0-9A-F]{2}), ([^}]*)\}")
BYTES = re.compile(r"const uint8_t k\w+?_?([0-9A-F]{2})\[\d+\] = \{([^}]*)\};")


def run(devicedata, chip):
    return subprocess.run([sys.executable, str(TOOL), "--devicedata", str(devicedata), chip],
                          capture_output=True, text=True, check=True).stdout


def table(text):
    """{addr: (width, reset bytes, flags)} from a C table."""
    resets = {a: bytes(int(b, 0) for b in body.replace(" ", "").split(",") if b)
              for a, body in BYTES.findall(text)}
    out = {}
    for addr, width, ref, flags in ENTRY.findall(text):
        names = sorted(f.strip().split("::")[-1] for f in flags.split("|") if f.strip() not in ("", "0"))
        out[int(addr, 16)] = (int(width), resets[ref], names)
    return out


def test_converter(tmp_path):
    (tmp_path / "knowledge").mkdir()
    (tmp_path / "knowledge" / "demo.yaml").write_text(KNOWLEDGE)
    text = run(tmp_path, "demo")
    assert table(text) == {
        0x10: (1, b"\x5a", []),
        0x20: (2, b"\x01\x02", ["kWritable"]),
        0x30: (1, b"\x00", ["kVolatile"]),
    }
    assert "static const RegTableSpec kDemoSpec = {kDemoEntries, 3, nullptr, 0, true};" in text
    assert "knowledge/demo.yaml" in text.splitlines()[1]


def test_qmp6988_model_matches_knowledge():
    devicedata = os.environ.get("EMBEDBENCH_DEVICEDATA")
    if not devicedata:
        pytest.skip("EMBEDBENCH_DEVICEDATA (a clone of EmbedBenchDeviceData) is not set")
    generated = table(run(devicedata, "qmp6988"))
    model = table(MODEL.read_text())
    assert generated and generated == model
