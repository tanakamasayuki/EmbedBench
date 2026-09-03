"""Evaluate fixed tick boundaries over arbitrary host wait slices."""


def test_tick_split(dut):
    dut.expect("TEST start tick_split", timeout=10)
    dut.expect("step1_now=2500 ticks=2 next=3000", timeout=10)
    dut.expect("step2_now=2999 ticks=2 next=3000", timeout=10)
    dut.expect("step3_now=3000 ticks=3 next=4000", timeout=10)
    dut.expect("yield_ticks=3 zero_waits=1 now=3000", timeout=10)
    dut.expect("delay_now=6000 ticks=6 next=7000", timeout=10)
    dut.expect("TEST done", timeout=10)

