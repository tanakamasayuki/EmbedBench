"""Format names with schema fingerprints and the length limit."""


def test_format_schema(dut):
    dut.expect("TEST start format_schema", timeout=10)
    dut.expect("values a=1 b=1 c=0 d=2 e=3 f=3 g=0 noformat=0 unknown=0 ok=1",
               timeout=10)
    dut.expect("01 000000 main diag diag.fmt_conflict name=acme.cmd.1",
               timeout=10)
    # A 20-character name is refused whole rather than clipped to 19.
    dut.expect("02 000000 main diag diag.fmt_name_long len=20", timeout=10)
    dut.expect("03 000000 main diag diag.frame_noformat bus=0", timeout=10)
    # A raw id that registerFormat never handed out cannot bypass the
    # name + schema check.
    dut.expect("04 000000 main diag diag.frame_unknown_format bus=0 fmt=7",
               timeout=10)
    dut.expect("05 000000 main app frame.tx bus=0 fmt=acme.cmd.1 bits=8 data=55",
               timeout=10)
    dut.expect("stats events=5 diag=4", timeout=10)
    dut.expect("TEST done", timeout=10)
