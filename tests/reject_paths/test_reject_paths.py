"""Measure how rejected or dropped operations appear to the current hooks."""


def test_reject_paths(dut):
    dut.expect("TEST start reject_paths", timeout=10)
    dut.expect("wire_no_begin status=4 hook_calls=0", timeout=10)
    dut.expect("wire_overflow status=1 hook_calls=0 accepted=128 attempted=200",
               timeout=10)
    dut.expect(
        "uart_overflow attempted=1200 written=1024 hook_events=11 "
        "hook_bytes=1024 overflow_flag=1",
        timeout=10)
    dut.expect(
        "analog_reject unattached_write=0 zero_freq=0 wide_res=0 "
        "events_during_rejects=0 valid_attach=1 events_after=1",
        timeout=10)
    dut.expect("TEST done", timeout=10)
