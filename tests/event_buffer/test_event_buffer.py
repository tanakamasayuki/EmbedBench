"""Measure event record size, overflow policies, and line format cost."""


def test_event_buffer(dut):
    dut.expect("TEST start event_buffer", timeout=10)
    dut.expect("event_bytes=32 capacity=8 buffer_bytes=256", timeout=10)
    dut.expect("drop_new stored=8 first=1 last=8 dropped=4 offered_last=12",
               timeout=10)
    dut.expect(
        "overwrite stored=8 first=5 last=12 overwritten=4 offered_first=1",
        timeout=10)
    dut.expect("gpio_bytes seq_first=59 time_first=59 json=94", timeout=10)
    dut.expect("i2c_bytes seq_first=68 json=109", timeout=10)
    # Wall-clock timings vary per machine; only require presence and shape.
    dut.expect(r"gen_100k_us seq_first=\d+ time_first=\d+ json=\d+",
               timeout=30)
    dut.expect(
        "gen_100k_bytes seq_first=5900000 time_first=5900000 json=9400000",
        timeout=10)
    dut.expect("TEST done", timeout=10)
