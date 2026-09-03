"""UART AT conversation golden: immediate and tick-delayed replies."""


def test_uart_golden(dut):
    dut.expect("TEST start uart_golden", timeout=10)
    dut.expect("01 000000 uart.begin", timeout=10)
    dut.expect("02 000000 uart.tx AT", timeout=10)
    dut.expect("03 000000 uart.rx O", timeout=10)
    dut.expect("04 000000 uart.rx K", timeout=10)
    dut.expect(r"05 002000 uart.tx AT\+S", timeout=10)
    dut.expect("06 003000 dir.inject OK", timeout=10)
    dut.expect("07 003000 uart.rx O", timeout=10)
    dut.expect("08 003000 uart.rx K", timeout=10)
    dut.expect("ex1 rx=OK elapsed=0 waits=0", timeout=10)
    dut.expect("ex2 rx=OK elapsed=1000", timeout=10)
    dut.expect("TEST done", timeout=10)
