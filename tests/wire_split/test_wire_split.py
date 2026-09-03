"""Split Wire hooks into observers plus a single responder per address."""


def test_wire_split(dut):
    dut.expect("TEST start wire_split", timeout=10)
    dut.expect("bind_first=1 bind_duplicate=0 diag_dup=1", timeout=10)
    dut.expect("case1_status=0 dev_writes=1 obs_a_w=1 obs_b_w=1", timeout=10)
    dut.expect("case2_len=2 rx=ACCE obs_a_r=1 obs_b_r=1", timeout=10)
    dut.expect("case3_status=2 diag_w=1 obs_a_w=2", timeout=10)
    dut.expect("case4_len=0 diag_r=1 obs_a_r=2", timeout=10)
    dut.expect("case5_status=0 dev0_writes=2 dev1_writes=0", timeout=10)
    dut.expect("TEST done", timeout=10)
