"""Draft core v2: analog, the lifecycle run window, and event listeners."""


def test_core_draft2(dut):
    dut.expect("TEST start core_draft2", timeout=10)
    # The application reads what the director injected; the fifth listener
    # is refused by the fixed table of four.
    dut.expect("values raw=1234 mv=3300 fifth=0 capacity=4", timeout=10)

    # The window opens at preSetup, without the application asking.
    dut.expect("01 000000 main core life.pre_setup", timeout=10)
    dut.expect("02 000000 main diag diag.listener_full cap=4", timeout=10)
    # Analog injection is director-side state; reads are request/response
    # pairs like the buses, and PWM writes are single events.
    dut.expect("03 000000 main dir analog.inject pin=8 val=1234", timeout=10)
    dut.expect("04 000000 main dir analog.inject.mv pin=8 mv=3300", timeout=10)
    dut.expect("05 000000 main app analog.config bits=10", timeout=10)
    dut.expect("06 000000 main app analog.req pin=8", timeout=10)
    dut.expect("07 000000 main core analog.resp val=1234 re=6", timeout=10)
    dut.expect("08 000000 main app analog.mv.req pin=8", timeout=10)
    dut.expect("09 000000 main core analog.mv.resp mv=3300 re=8", timeout=10)
    # analogWrite on an unattached pin is an attach followed by a write,
    # exactly as the host core reports what silicon would have done.
    dut.expect("10 000000 main app analog.out attach pin=9 duty=0 hz=1000",
               timeout=10)
    dut.expect("11 000000 main app analog.out write pin=9 duty=128 hz=1000",
               timeout=10)

    # Lifecycle phases bracket the run, and the core closes the window at
    # the second postLoop on its own.
    dut.expect("12 000000 main core life.post_setup", timeout=10)
    dut.expect("13 000000 main core life.pre_loop n=1", timeout=10)
    dut.expect("14 000000 main app gpio.write pin=4 val=1", timeout=10)
    dut.expect("15 001000 main core life.post_loop n=1", timeout=10)
    dut.expect("18 002000 main core life.post_loop n=2", timeout=10)
    dut.expect("19 002000 main core life.window_end loops=2", timeout=10)

    # The third loop iteration left nothing behind: 19 events, nothing
    # dropped, and the window reports itself closed.
    dut.expect("stats events=19 dropped=0 closed=1 loops=2", timeout=10)
    # Both observers saw every event after they were installed; the
    # self-removing one saw exactly one and then stopped.
    dut.expect("listeners a=18 b=18 once=1 last_a=life.window_end loops=2",
               timeout=10)
    dut.expect("TEST done", timeout=10)
