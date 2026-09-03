"""Compare event-completion policies under a nested sink call."""


def test_event_timing(dut):
    dut.expect("TEST start event_timing", timeout=10)

    # (a) two lines: the nested event sits between request and response,
    # so causality is visible and the stream stays in seq order.
    dut.expect(
        "== two_lines lines=3 stream=123 in_order=1 exposure=0 rejected=0 "
        "deferred=0 pin_cb=1 pin_final=1",
        timeout=10)
    dut.expect("log 1 i2c.req addr=48 req=1", timeout=10)
    dut.expect("log 2 gpio.inject pin=7 val=1", timeout=10)
    dut.expect("log 3 i2c.resp addr=48 data=2C", timeout=10)

    # (b) reserve: one line per op, but completion order inverts (21), so
    # a streaming sink emits out of seq order and the nested event
    # completes while an earlier slot is still incomplete.
    dut.expect(
        "== reserve lines=2 stream=21 in_order=0 exposure=1 rejected=0 "
        "deferred=0 pin_cb=1 pin_final=1",
        timeout=10)
    dut.expect("log 1 i2c.read addr=48 data=2C", timeout=10)
    dut.expect("log 2 gpio.inject pin=7 val=1", timeout=10)

    # (c) forbid: clean ordering, but the device's IRQ line never moves —
    # reactive device models lose function.
    dut.expect(
        "== forbid lines=2 stream=12 in_order=1 exposure=0 rejected=1 "
        "deferred=0 pin_cb=0 pin_final=0",
        timeout=10)
    dut.expect("log 1 i2c.read addr=48 data=2C", timeout=10)
    dut.expect("log 2 diag.reject sink_in_response", timeout=10)

    # (c') defer: ordering and function kept, but the pin applies late —
    # the device reads its own line stale inside the callback.
    dut.expect(
        "== defer lines=2 stream=12 in_order=1 exposure=0 rejected=0 "
        "deferred=1 pin_cb=0 pin_final=1",
        timeout=10)
    dut.expect("log 1 i2c.read addr=48 data=2C", timeout=10)
    dut.expect("log 2 gpio.inject pin=7 val=1 deferred=1", timeout=10)

    dut.expect("TEST done", timeout=10)
