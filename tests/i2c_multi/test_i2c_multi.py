"""Repeated start is bus state: another address's STOP closes it."""


def test_i2c_multi(dut):
    dut.expect("TEST start i2c_multi", timeout=10)
    dut.expect("values broken_len=0 proper_len=1", timeout=10)
    dut.expect("01 000000 main app i2c.req addr=50 data=01 stop=0", timeout=10)
    dut.expect("02 000000 main dev i2c.resp status=0 re=1", timeout=10)
    dut.expect("03 000000 main app i2c.req addr=51 data=01 stop=1", timeout=10)
    dut.expect("04 000000 main dev i2c.resp status=0 re=3", timeout=10)
    # B's STOP closed the bus: A's read is not a repeated start (no "rs").
    dut.expect("05 000000 main app i2c.rd.req addr=50 req=1 stop=1", timeout=10)
    dut.expect("06 000000 main dev i2c.rd.resp len=0 data= re=5", timeout=10)
    dut.expect("07 000000 main app i2c.req addr=50 data=01 stop=0", timeout=10)
    dut.expect("08 000000 main dev i2c.resp status=0 re=7", timeout=10)
    dut.expect("09 000000 main app i2c.rd.req addr=50 req=1 stop=1 rs", timeout=10)
    dut.expect("10 000000 main dev i2c.rd.resp len=1 data=5A re=9", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
