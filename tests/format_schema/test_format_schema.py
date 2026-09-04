"""Format names with schema fingerprints: idempotent, conflict-diagnosed."""


def test_format_schema(dut):
    dut.expect("TEST start format_schema", timeout=10)
    dut.expect("values a=1 b=1 c=0 d=2 noformat=0 ok=1", timeout=10)
    dut.expect("01 000000 main diag diag.fmt_conflict name=acme.cmd.1",
               timeout=10)
    dut.expect("02 000000 main diag diag.frame_noformat bus=0", timeout=10)
    dut.expect("03 000000 main app frame.tx bus=0 fmt=acme.cmd.1 bits=8 data=55",
               timeout=10)
    dut.expect("stats events=3 diag=2", timeout=10)
    dut.expect("TEST done", timeout=10)
