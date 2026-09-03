"""WP-C1 vertical slice: unmodified app, zero event loss, 3x reproducible."""


def test_i2c_slice(dut):
    dut.expect("TEST start i2c_slice", timeout=10)
    # App-visible values plus zero gap between injected mutations and
    # recorded injection events.
    dut.expect("before=250 after=300 gap=0", timeout=10)
    # The full golden event list of run 1: 250 = 0x00FA, 300 = 0x012C.
    dut.expect("01 000000 main app i2c.write addr=48 data=0105", timeout=10)
    dut.expect("02 000000 main app i2c.write addr=48 data=00", timeout=10)
    dut.expect("03 000000 main app i2c.read addr=48 data=00FA", timeout=10)
    dut.expect("04 002000 tick dir chan.write chan=0 data=012C", timeout=10)
    dut.expect("05 003000 main app i2c.write addr=48 data=00", timeout=10)
    dut.expect("06 003000 main app i2c.read addr=48 data=012C", timeout=10)
    dut.expect("07 003000 main dir dump temp=012C cfg=05", timeout=10)
    # Runs 2 and 3 must be byte-identical to run 1.
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
