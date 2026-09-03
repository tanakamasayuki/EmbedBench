"""WP-A2: compare direct, common-interface, and split routing policies."""

import re
from pathlib import Path

SKETCH = Path(__file__).parent / "device_route.ino"


def region_loc(name: str) -> int:
    """Count code lines between [name begin] and [name end] markers."""
    text = SKETCH.read_text()
    match = re.search(
        rf"// \[{re.escape(name)} begin\]\n(.*?)// \[{re.escape(name)} end\]",
        text,
        re.DOTALL,
    )
    assert match, f"missing markers for {name}"
    lines = [
        line
        for line in match.group(1).splitlines()
        if line.strip() and not line.strip().startswith("//")
    ]
    return len(lines)


def test_device_route(dut):
    dut.expect("TEST start device_route", timeout=10)
    dut.expect(
        "route1_direct temp=300 cfg=5 events=3 trace=<wwr> "
        "channel_cb=0 conv=0 gap=1",
        timeout=10)
    dut.expect(
        "route2_common temp=300 cfg=5 events=6 trace=<swjwri> "
        "channel_cb=3 conv=6 gap=0",
        timeout=10)
    dut.expect(
        "route3_split temp=300 cfg=5 events=4 trace=<wjwr> "
        "channel_cb=1 conv=2 gap=0",
        timeout=10)
    dut.expect("TEST done", timeout=10)

    loc = {
        "route1_caller": region_loc("route1-caller"),
        "route2_caller": region_loc("route2-caller"),
        "route3_caller": region_loc("route3-caller"),
        "device_typed_api": region_loc("device-typed-api"),
        "device_channel_adapter": region_loc("device-channel-adapter"),
    }
    print(f"LOC {loc}")
    assert loc == {
        "route1_caller": 10,
        "route2_caller": 15,
        "route3_caller": 12,
        "device_typed_api": 6,
        "device_channel_adapter": 20,
    }
