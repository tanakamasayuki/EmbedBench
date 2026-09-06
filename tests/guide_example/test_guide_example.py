"""The getting-started guide's example, built and run so it cannot rot.

The sketch in this directory is the code block from docs/GUIDE.md
verbatim, and the expectations below are the ones the guide prints. The
first check is that the two are still identical: a guide whose example
does not compile is worse than no guide.
"""

import re
from pathlib import Path

HERE = Path(__file__).parent
DOCS = HERE.parent.parent / "docs"
MARKER = "// Built from the code block in docs/GUIDE.md"


def guide_block(path: Path, fence: str) -> str:
    blocks = re.findall(rf"```{fence}\n(.*?)```", path.read_text(), re.S)
    assert len(blocks) == 1, f"expected one {fence} block in {path.name}"
    return blocks[0]


def test_guide_matches_sketch():
    # The sketch is the guide's block with only the test name changed.
    expected = guide_block(DOCS / "GUIDE.md", "cpp").replace(
        "TEST start myexperiment", "TEST start guide_example")
    actual = (HERE / "guide_example.ino").read_text()
    assert actual.startswith(MARKER), "the provenance comment is missing"
    body = actual.split("\n", 2)[2]
    assert body == expected, (
        "docs/GUIDE.md and guide_example.ino have drifted apart")

    # The expectations in this file are the guide's, line for line.
    doc_expect = guide_block(DOCS / "GUIDE.md", "python").replace(
        "myexperiment", "guide_example")
    here = (HERE / "test_guide_example.py").read_text()
    for line in doc_expect.splitlines():
        if line.strip().startswith("dut.expect"):
            assert line.strip() in here, f"guide asserts a line this test does not: {line.strip()}"

    # The Japanese guide carries the same example with translated
    # comments, so check the code that matters is the same in both.
    ja = guide_block(DOCS / "GUIDE.ja.md", "cpp")
    for needle in ["static TempSensorModel sensor;",
                   "static ebhost::DevicePort port;",
                   "port.mapLine(TempSensorModel::kLineDataReady, 27);",
                   "ebhost::bindWireDevice(0x48, ops);",
                   "ebhost::runBegin(1000);"]:
        assert needle in ja, f"GUIDE.ja.md lost: {needle}"


def test_guide_example(dut):
    dut.expect("TEST start guide_example", timeout=10)
    dut.expect("values temp=00FA", timeout=10)
    dut.expect("01 000000 main dir chan.write chan=0 data=00FA", timeout=10)
    dut.expect("02 000000 main dev gpio.inject pin=27 0->1 match=0", timeout=10)
    dut.expect("06 000000 main dev i2c.rd.resp len=2 data=00FA re=5", timeout=10)
    dut.expect("stats events=6 dropped=0 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
