"""Simultaneous effects, a shared signal line, and a shortened wait."""


def test_units_race(dut):
    dut.expect("TEST start units_race", timeout=10)

    # Two sensors on one input with overlapping hold windows. The early
    # one releases at 2500 and the late one at 3500, so the line must
    # stay up through 2600 and only fall after 3500. `naive` is what a
    # pass-through adapter would have shown at 2600: low, because the
    # first sensor to let go would have pulled the line out from under
    # the one still asserting.
    dut.expect("values at2400=1 at2600=1 at3600=0 naive=0", timeout=10)
    # A known limitation, measured rather than left to be discovered: a
    # delayMicroseconds spanning a wake comes back early, because it is
    # the one waiter in the core that does not loop to its deadline.
    dut.expect("values asked=200 took=100", timeout=10)

    # One sensor asserting is one transition on the shared line; the
    # second one asserting adds nothing, and neither does the first one
    # releasing while the second still holds.
    dut.expect("02 000000 main dev gpio.inject pin=26 0->1 match=0",
               timeout=10)

    # Three parts were arranged to act on the very same microsecond. The
    # order is the adapter's iteration order, not something the
    # environment picks — which is what makes it reproducible.
    dut.expect("09 002500 tick dev dev.frame bus=0 fmt=m5.ir.rep.1 bits=0 "
               "empty", timeout=10)
    dut.expect("10 002500 tick dev dev.tx len=7 crc=AC", timeout=10)
    # The early sensor's release also fell on 2500 and produced no event
    # at all, because the combined line did not change.
    dut.expect("11 002500 main app gpio.read pin=26 val=1", timeout=10)
    # Only when the late sensor lets go does the line fall.
    dut.expect("12 003500 tick dev gpio.inject pin=26 1->0 match=0",
               timeout=10)

    dut.expect("stats events=13 dropped=0 folded=0 diag=0", timeout=10)
    # Same instant, same order, three runs running.
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
