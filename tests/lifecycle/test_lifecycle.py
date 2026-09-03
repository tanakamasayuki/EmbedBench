"""Pin the host lifecycle order without defining EmbedBench semantics."""


def test_lifecycle(dut):
    dut.expect("TEST start lifecycle", timeout=10)
    dut.expect("sequence=ASBCLDCL", timeout=10)
    dut.expect("sequence_length=8", timeout=10)
    dut.expect("completed_loops_before_return=1", timeout=10)
    dut.expect("TEST done", timeout=10)

