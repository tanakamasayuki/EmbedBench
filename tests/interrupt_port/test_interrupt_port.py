"""Verify the 1.7.1 interrupt port properties requested as H1."""


def test_interrupt_port(dut):
    dut.expect("TEST start interrupt_port", timeout=10)
    # RISING is raw 3 on this core (arduino-esp32 uses 1); the normalized
    # trigger enum (kTriggerRising == 1) is the safe thing to match.
    dut.expect("attach mode_raw=3 trigger=1 trace=<A>", timeout=10)
    dut.expect("no_auto_fire fires=0", timeout=10)
    dut.expect("sync_fire ok=1 fires=1 trace=<E1wX0>", timeout=10)
    dut.expect("nested fires=2 max_depth=2 trace=<E1E2X1X0>", timeout=10)
    dut.expect("self_detach trace=<E1DX0> retrig=0 attached=0", timeout=10)
    dut.expect("arg_handler count=1 mode_raw=2 trigger=2", timeout=10)
    # CHANGE is raw 1 on this core (arduino-esp32 uses 3).
    dut.expect("rearm fires=2 mode_raw=1 trigger=3 trace=<A> detach_delta=0",
               timeout=10)
    dut.expect("unattached trig=0 detach_delta=0", timeout=10)
    dut.expect("TEST done", timeout=10)
