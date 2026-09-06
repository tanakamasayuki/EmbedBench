"""Two instances of each bus at once, one device per endpoint."""


def test_multi_bus(dut):
    dut.expect("TEST start multi_bus", timeout=10)
    # The same address on two Wire instances holds two different devices,
    # and each serial port has its own modem.
    dut.expect("values a=A1 b=B2 r1=OK r2=OK", timeout=10)

    # Wire (instance 0) keeps the lines it always had; only the second
    # instance is tagged, so existing traces are unchanged.
    dut.expect("01 000000 main app i2c.req addr=50 data=01A1 stop=1",
               timeout=10)
    dut.expect("03 000000 main app i2c.req addr=50 bus=1 data=01B2 stop=1",
               timeout=10)
    # Repeated start is tracked per bus: a transfer on Wire1 in between
    # does not close the sequence Wire had open, and each device answers
    # from its own contents.
    dut.expect("07 000000 main app i2c.rd.req addr=50 req=1 stop=1 rs",
               timeout=10)
    dut.expect("08 000000 main dev i2c.rd.resp len=1 data=A1 re=7", timeout=10)
    dut.expect("11 000000 main app i2c.rd.req addr=50 bus=1 req=1 stop=1 rs",
               timeout=10)
    dut.expect("12 000000 main dev i2c.rd.resp len=1 data=B2 re=11",
               timeout=10)

    # Each serial port routes to its own device, and the replies come back
    # on the port they belong to.
    dut.expect("13 000000 main app uart.tx AT;", timeout=10)
    dut.expect("14 000000 main dev dev.tx OK", timeout=10)
    dut.expect("15 000000 main app uart.tx port=2 AT;", timeout=10)
    dut.expect("16 000000 main dev dev.tx port=2 OK", timeout=10)
    dut.expect("19 000000 main app uart.rx port=2 O", timeout=10)

    dut.expect("stats events=20 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
