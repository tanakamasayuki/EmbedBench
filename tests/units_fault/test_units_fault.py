"""Failure paths, and what survives a power cycle."""

import re


def test_units_fault(dut):
    dut.expect("TEST start units_fault", timeout=10)

    # Four outcomes a defensive driver has to tell apart: served, address
    # not acknowledged (2), payload rejected (3), and a read that came
    # back short (0x80 | 1 byte where four were asked for). The last is
    # the one with no error status attached to it — a read has nowhere to
    # report one — so only the byte count gives it away.
    dut.expect("values ok=00 absent=02 refuse=03 short=81 back=00", timeout=10)
    # Intermittent: two transactions fail and the third is served, with
    # nothing in the sketch doing anything about it.
    dut.expect("values flaky=02,02,00", timeout=10)
    # The byte programmed before the cycle is still there after it, and
    # only a chip erase puts it back to 0xFF.
    dut.expect("values cycle=5A,5A,FF", timeout=10)

    dut.expect("04 000000 main dev i2c.rd.resp len=4 data=A0A1A2A3 re=3",
               timeout=10)
    # Absent: the write never gets an acknowledgement, so the driver stops
    # before the read and no read appears in the trace at all.
    dut.expect("07 000000 main dev i2c.resp status=2 re=6", timeout=10)
    dut.expect("10 000000 main dev i2c.resp status=3 re=9", timeout=10)
    # Short read: the status was fine and one byte came back.
    dut.expect("13 000000 main dev i2c.resp status=0 re=12", timeout=10)
    dut.expect("15 000000 main dev i2c.rd.resp len=1 data=A0 re=14",
               timeout=10)
    # Two failures fold into one line with the count; the third
    # transaction is served without the sketch intervening.
    dut.expect(re.escape("18 000000 main dev i2c.resp status=2 re=17 "
                         "x2..000000"), timeout=10)
    dut.expect("24 000000 main dev i2c.rd.resp len=4 data=A0A1A2A3 re=23",
               timeout=10)

    # The power cycle, and the point of the whole thing: the read before
    # it and the read after it are the same event, so they fold into one
    # line saying it happened twice.
    dut.expect(re.escape("50 004000 main dev spi.resp miso=5A re=49 "
                         "x2..004000"), timeout=10)
    # A chip erase is the only way back to a blank part.
    dut.expect("65 004000 main app spi.req mosi=C7", timeout=10)
    dut.expect("74 013000 main dev spi.resp miso=FF re=73", timeout=10)

    # Seven transactions served and six refused, and the flash counter is
    # back to one because the power cycle cleared it — the array is
    # non-volatile, the bookkeeping around it is not.
    dut.expect("76 013000 main dir dump faulty mode=0 left=0 ok=7 bad=6",
               timeout=10)
    dut.expect("77 013000 main dir dump flash we=0 busy=0 progs=1 no=0 "
               "m0=FF m1=FF", timeout=10)
    dut.expect("stats events=63 dropped=0 folded=14 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
