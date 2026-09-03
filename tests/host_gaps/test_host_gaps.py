"""Keep known host-core observability gaps explicit and reproducible."""


def test_host_gaps(dut):
    dut.expect("TEST start host_gaps", timeout=10)
    dut.expect("interrupt_edges=2 callback_calls=0", timeout=10)
    dut.expect("analog_raw=1234 mv=3300 hook_after_raw=1 hook_after_mv=1", timeout=10)
    dut.expect("uart_tx_delta=2 queued=2 drained=2 bytes=AT activity_hook=0", timeout=10)
    dut.expect("TEST done", timeout=10)

