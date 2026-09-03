"""Record the exact wait-port behavior of host-arduino-core 1.7.0."""


def test_clock(dut):
    dut.expect("TEST start clock", timeout=10)
    dut.expect("delay_us=3000 calls=3 zero=0 one_ms=3 other=0", timeout=10)
    dut.expect("delay_micro_us=2500 calls=1 zero=0 one_ms=0 other=1", timeout=10)
    dut.expect("delay_zero_calls=0", timeout=10)
    dut.expect("zero_wait_calls=2 total_calls=2", timeout=10)
    dut.expect("stream_timeout_us=4000 calls=4 received=0", timeout=10)
    dut.expect("TEST done", timeout=10)

