"""Confirm the replacement behavior of the host core's single hook slots."""


def test_hook_slots(dut):
    dut.expect("TEST start hook_slots", timeout=10)
    dut.expect("pin_a=1 pin_b=1 physical_writes=3", timeout=10)
    dut.expect("clock_a_calls=0 clock_b_calls=1 clock_a_us=0 clock_b_us=7", timeout=10)
    dut.expect("lifecycle_a=0 lifecycle_b_before_post=3", timeout=10)
    dut.expect("TEST done", timeout=10)

