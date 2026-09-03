"""Compare policies for a wait API called from inside a tick callback."""


def test_tick_guard(dut):
    dut.expect("TEST start tick_guard", timeout=10)
    dut.expect(
        "s1_allow_delay ticks=5 depth=2 late=0 rejected=0 rejected_us=0 "
        "cap=0 backward=0 zero=0 now=5000 "
        "order=1@1000 2@2000 3@3000 4@4000 5@5000",
        timeout=10)
    dut.expect(
        "s2_defer_delay ticks=5 depth=1 late=2 rejected=0 rejected_us=0 "
        "cap=0 backward=0 zero=0 now=5000 "
        "order=1@1000 2@2000 3@4000 4@4000 5@5000",
        timeout=10)
    dut.expect(
        "s3_reject_delay ticks=5 depth=1 late=0 rejected=51 rejected_us=51000 "
        "cap=2 backward=1 zero=0 now=5000 "
        "order=1@1000 2@2000 3@3000 4@4000 5@5000",
        timeout=10)
    dut.expect(
        "s4_reject_micros ticks=5 depth=1 late=0 rejected=1 rejected_us=500 "
        "cap=0 backward=0 zero=0 now=5000 "
        "order=1@1000 2@2000 3@3000 4@4000 5@5000",
        timeout=10)
    dut.expect(
        "s5_reject_yield ticks=5 depth=1 late=0 rejected=0 rejected_us=0 "
        "cap=0 backward=0 zero=1 now=5000 "
        "order=1@1000 2@2000 3@3000 4@4000 5@5000",
        timeout=10)
    dut.expect("TEST done", timeout=10)
