"""Verify the 1.7.1 UART activity hook properties requested as H2."""


def test_uart_activity(dut):
    dut.expect("TEST start uart_activity", timeout=10)
    dut.expect("begin trace=<B> uart_num=1", timeout=10)
    dut.expect("config trace=<C>", timeout=10)
    # TX lands between the two GPIO writes — the ordering X3/X6 lost — and
    # the bytes still reach the tx queue for polling.
    dut.expect("tx_order trace=<wT2w> tx_avail=2", timeout=10)
    # The hook's pushRx answered before write() returned, so the blocking
    # read needs zero waits (X3 measured 1 wait / 1,000 us for the same
    # exchange), and each consumed byte is one kUartRx event.
    dut.expect("reply len=2 rx=OK wait_calls=0 trace=<RR>", timeout=10)
    dut.expect("drain len=2 bytes=AT trace=<>", timeout=10)
    dut.expect("discard trace=<F4>", timeout=10)
    dut.expect("end trace=<N>", timeout=10)
    dut.expect("TEST done", timeout=10)
