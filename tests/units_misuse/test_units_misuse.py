"""Driving the library wrong on purpose: every mistake must leave evidence."""


def test_units_misuse(dut):
    dut.expect("TEST start units_misuse", timeout=10)

    # Before runBegin the environment is not connected to anything, so
    # the Wire transfer got the host core's own answer (2, nobody home)
    # and left no trace. windows0=0 is the evidence that matters: an
    # empty trace with no window ever opened means "never started", not
    # "nothing happened". A direct sink call is visible even then, and
    # both of them — one before the window and one after — are counted.
    dut.expect("values before=2 windows0=0 outside=1,2 unbound=2,0",
               timeout=10)
    # Reading without selecting a register first is the one mistake here
    # with no diagnostic attached: the bus was fine and the part answered
    # with whatever register was last set. No status code can report a
    # sketch asking the wrong question, so only an assertion on the value
    # catches it — which is the argument for asserting on values and not
    # just on the absence of errors.
    dut.expect("values stale=0100 spi=FF frame=0 fmt=0", timeout=10)

    # An address nobody is bound to: the host core has no opinion, so the
    # environment supplies one, on the write and on the read.
    dut.expect("02 000000 main diag diag.unbound addr=77 re=1", timeout=10)
    dut.expect("03 000000 main dev i2c.resp status=2 re=1", timeout=10)
    dut.expect("05 000000 main diag diag.unbound addr=77 re=4", timeout=10)
    dut.expect("06 000000 main dev i2c.rd.resp len=0 data= re=4", timeout=10)
    # The register-less read: recorded as an ordinary, successful transfer.
    dut.expect("08 000000 main dev i2c.rd.resp len=2 data=0100 re=7",
               timeout=10)
    # A channel nobody handles.
    dut.expect("10 000000 main diag diag.chan_reject chan=9 len=1", timeout=10)
    # SPI with no device bound at all.
    dut.expect("12 000000 main diag diag.unbound spi re=11", timeout=10)
    # A format id that was never registered, and a name over the limit.
    dut.expect("14 000000 main diag diag.frame_unknown_format bus=0 fmt=4660",
               timeout=10)
    dut.expect("15 000000 main diag diag.fmt_name_long len=25", timeout=10)

    dut.expect("stats events=15 dropped=0 diag=6 outside=2 windows=1",
               timeout=10)
    dut.expect("TEST done", timeout=10)
