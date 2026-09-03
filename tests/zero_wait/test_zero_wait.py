"""Probe re-entry and progress conditions for 0 us wait processing."""


def test_zero_wait(dut):
    dut.expect("TEST start zero_wait", timeout=10)
    dut.expect("case1_spins=5 zero_waits=5 max_depth=2 reentries=1 now=0",
               timeout=10)
    dut.expect("case2_spins=50 zero_wait_delta=50 now=0", timeout=10)
    dut.expect("case3_zero_delta=0 timed_delta=0", timeout=10)
    dut.expect("TEST done", timeout=10)
