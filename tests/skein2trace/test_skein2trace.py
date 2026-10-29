"""X66: wireskein's decode of an OEP probe's capture, as event lines.

The sample is a real one: a P4X probe ran a library case writing two
bytes to an absent 0x50 with no pull-ups, captured SDA/SCL itself at
1 MHz, and logged the case markers on the capture's clock. Its run.json,
capture, and wireskein's transactions (with start_cond / end_cond /
scl_waits, wireskein e2ced47) are kept here so the format is pinned by
real output, not by a guess at it (the lesson of X65's sigrok shift).
"""

import importlib.util
import json
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
TOOLS = ROOT / "devices" / "tools"
SAMPLE = HERE / "libtest_run1"


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def sample_lines():
    tool = load("skein2trace")
    i2c = json.loads((SAMPLE / "i2c.json").read_text())
    run = json.loads((SAMPLE / "run.json").read_text())
    return tool.convert(i2c, SAMPLE / "c0001.wireskein", run)


def test_real_sample():
    lines = sample_lines()
    assert lines == [
        "6055947 mark case=test_wire clock=host",
        "6061895 mark case=test_wire phase=write_2_bytes_to_0x50",
        "6062119 i2c.req addr=50 data= stop=1",
        "6062119 i2c.resp status=2",
        "6062291 mark src=dut text=endTransmission%3D4",
        "6062294 mark case=test_wire phase=write_2_bytes_to_0x50 end=1",
        "6132080 mark case=test_wire end=1 clock=host",
    ]
    # One clock: the transfer (tick 211 of a capture starting at probe
    # time 6061908 us) sits between the phase's markers, and the
    # application's line lands where wireskein's own marker table puts
    # it (tick 383).
    # The bus says address NACK (2) where the library's Wire said 4: the
    # lines carry the bus's view.


def test_generators_skip_markers():
    """The scaffold reads the transfers and nothing from the markers,
    even though a marker names the phase after a write."""
    t2r = load("trace2regtable")
    events = t2r.parse(sample_lines())
    assert [(e.time, e.kind) for e in events] == [
        (6062119, "i2c.req"), (6062119, "i2c.resp")]


def test_read_rs_waits_and_nack():
    """The shapes the sample lacks, on the sample's clock: a pointer write
    ending in a repeated start, the read it continues with a long SCL
    low, and a data NACK."""
    tool = load("skein2trace")
    i2c = {"transactions": [
        {"s": 1000, "e": 1100, "addr": 0x44, "rw": "write", "addr_ack": True,
         "bytes": "2400", "acks": [True, True], "start_cond": "S",
         "end_cond": "Sr", "scl_low_ticks": 5.0, "scl_waits": []},
        {"s": 1110, "e": 9000, "addr": 0x44, "rw": "read", "addr_ack": True,
         "bytes": "61a2b56e0f3c", "acks": [True] * 5 + [False],
         "start_cond": "Sr", "end_cond": "P", "scl_low_ticks": 5.0,
         "scl_waits": [{"after_word": 0, "bit": 0, "ticks": 6759,
                        "s": 1200}]},
        {"s": 9500, "e": 9600, "addr": 0x44, "rw": "write", "addr_ack": True,
         "bytes": "30a2", "acks": [True, False], "start_cond": "S",
         "end_cond": None, "scl_low_ticks": 5.0, "scl_waits": []},
    ]}
    lines = tool.convert(i2c, SAMPLE / "c0001.wireskein")
    assert lines == [
        "6062908 i2c.req addr=44 data=2400 stop=0",
        "6062908 i2c.resp status=0",
        "6063018 i2c.rd.req addr=44 req=6 stop=1 rs",
        "6063018 i2c.rd.resp len=6 data=61A2B56E0F3C scl_wait=6759",
        "6071408 i2c.req addr=44 data=30A2 stop=1",
        "6071408 i2c.resp status=3",
    ]
    t2r = load("trace2regtable")
    events = t2r.parse(lines)
    assert events[3].fields["scl_wait"] == "6759"
    assert "rs" in events[2].flags


def test_marker_values_never_read_as_events():
    tool = load("skein2trace")
    run = {"captures": [{"file": "c0001.wireskein", "t0": 0.0}],
           "log": [{"t": 0.0, "src": "marker", "text": "# i2c.req",
                    "probe_ns": 6061908000},
                   {"t": 0.0, "src": "dut", "text": "x i2c.rd.resp y",
                    "probe_ns": 6061909000}]}
    lines = tool.convert({"transactions": []}, SAMPLE / "c0001.wireskein",
                         run)
    assert lines == ["6061908 mark case=i2c_req",
                     "6061909 mark src=dut text=x%20i2c.rd.resp%20y"]
    assert load("trace2regtable").parse(lines) == []
