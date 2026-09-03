"""Measure the host core's observation, injection, and response paths."""


def test_ports(dut):
    dut.expect("TEST start ports", timeout=10)
    dut.expect("gpio_write_events=2 injected=1 hooked=0", timeout=10)
    dut.expect("analog_raw=1234 hooked=617 mv=3300", timeout=10)
    dut.expect("spi_reply=5A transfers=1 edges=2 clock=8000000 mode=0", timeout=10)
    dut.expect(
        "wire_status=0 writes=1 reads=1 addr=34 tx_len=2 tx=ABCD rx_len=2 rx=1122",
        timeout=10,
    )
    dut.expect("uart_rx=2 reply=OK wait_calls=1 elapsed_us=1000", timeout=10)
    dut.expect("TEST done", timeout=10)

