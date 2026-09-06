"""Interface revisions 002-004 end to end on the host environment."""


def test_if_rev1(dut):
    dut.expect("TEST start if_rev1", timeout=10)
    # The application reads the voltage the device presented, and the reply
    # arrives 1500 us after the command — not rounded up to the 2000 us
    # tick the environment would otherwise have used.
    dut.expect("values raw=1234 reply=OK elapsed=1500", timeout=10)
    dut.expect("01 000000 main dir chan.write chan=0 data=04D2", timeout=10)
    # Revision 002: analogOut lands as a dev-origin injection, with no
    # director step between the device and what the application reads.
    dut.expect("02 000000 main dev analog.inject pin=8 val=1234", timeout=10)
    dut.expect("03 000000 main app analog.req pin=8", timeout=10)
    dut.expect("04 000000 main core analog.resp val=1234 re=3", timeout=10)
    dut.expect(r"05 000000 main app uart.tx \?", timeout=10)
    # Revision 004: the device's refusal is in the log, in order, at the
    # moment it happened — serial gave it no other way to say so.
    dut.expect("06 000000 main dev dev.note unknown command", timeout=10)
    dut.expect("07 000000 main app uart.tx g", timeout=10)
    # Revision 003: the reply is produced from the requested wake, so it
    # carries the director context the environment gives scheduled
    # advances, and it lands at 1500 us rather than the next tick.
    dut.expect("08 001500 tick dev dev.tx OK", timeout=10)
    dut.expect("09 001500 main app uart.rx O", timeout=10)
    dut.expect("10 001500 main app uart.rx K", timeout=10)
    dut.expect("11 001500 main dir dump rev1 raw=1234 pushes=1 notes=1 wake=1",
               timeout=10)
    dut.expect("TEST done", timeout=10)
