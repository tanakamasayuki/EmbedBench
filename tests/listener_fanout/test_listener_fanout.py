"""Fan one host hook out to fixed listener slots and probe the limits."""


def test_listener_fanout(dut):
    dut.expect("TEST start listener_fanout", timeout=10)
    dut.expect("p0_events=1 order=<>", timeout=10)
    dut.expect("p1_a=1 order=<A>", timeout=10)
    dut.expect("p2_a=2 b=1 c=1 d=1 order=<ABCD>", timeout=10)
    dut.expect("p3_added=0 rejected=1 e=0 order=<ABCD>", timeout=10)
    dut.expect("p4_b=2 order=<ACD>", timeout=10)
    dut.expect("p5_first_order=<ASCD>", timeout=10)
    dut.expect("p5_second_s=1 order=<ACD>", timeout=10)
    dut.expect("p6_d_delta=0 order=<ARC>", timeout=10)
    dut.expect("state_bytes=72 events=8", timeout=10)
    dut.expect("TEST done", timeout=10)
