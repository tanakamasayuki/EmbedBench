"""UART replies via the core RX sink: device send times enter the log."""


def test_uart_sink(dut):
    dut.expect("TEST start uart_sink", timeout=10)
    dut.expect("01 000000 uart.begin", timeout=10)
    dut.expect("02 000000 uart.tx AT", timeout=10)
    # The device's transmission is its own event, between the app's TX and
    # the app's RX consumption — X17 could not show this line.
    dut.expect("03 000000 dev.tx OK", timeout=10)
    dut.expect("04 000000 uart.rx O", timeout=10)
    dut.expect("05 000000 uart.rx K", timeout=10)
    dut.expect(r"06 002000 uart.tx AT\+S", timeout=10)
    dut.expect("07 003000 dev.tx OK", timeout=10)
    dut.expect("08 003000 uart.rx O", timeout=10)
    dut.expect("09 003000 uart.rx K", timeout=10)
    dut.expect("ex1 rx=OK elapsed=0 waits=0", timeout=10)
    dut.expect("ex2 rx=OK elapsed=1000", timeout=10)
    dut.expect("TEST done", timeout=10)
