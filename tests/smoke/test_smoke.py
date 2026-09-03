"""Verify the minimal Arduino/host/pytest experiment path."""


def test_smoke(dut):
    dut.expect("TEST start smoke", timeout=10)
    dut.expect("library_version=0.0.0", timeout=10)
    dut.expect("clock_overridden=1", timeout=10)
    dut.expect("delay_elapsed_us=3000", timeout=10)
    dut.expect("wait_calls=3", timeout=10)
    dut.expect("phases=1,1,2,1", timeout=10)
    dut.expect("TEST done", timeout=10)

