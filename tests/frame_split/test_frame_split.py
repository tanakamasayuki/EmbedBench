"""Per-bus frame capacity, and where the splitting rule lives."""


def test_frame_split(dut):
    dut.expect("TEST start frame_split", timeout=10)

    # The same twenty-byte message on three links of different width.
    # Six payload bytes a frame on the wide link, two on the small one,
    # and the narrow link cannot carry the header at all. Both messages
    # that went out came back whole.
    dut.expect("values wide=4,1 small=10,1 narrow=0", timeout=10)
    dut.expect("values caps=16,32,64", timeout=10)

    # The wide link: four chunks, one every 500 us because a link with a
    # limit on frame size has a cost per frame too. The last one is
    # shorter — two payload bytes left over — and carries more=0.
    dut.expect("02 000000 main dev dev.frame bus=2 fmt=m5.chunk.1 bits=64",
               timeout=10)
    dut.expect("03 000500 tick dev dev.frame bus=2 fmt=m5.chunk.1 bits=64",
               timeout=10)
    dut.expect("05 001500 tick dev dev.frame bus=2 fmt=m5.chunk.1 bits=32 "
               "data=0300B2B3", timeout=10)

    # The small link: the same message, ten chunks, sequence numbers
    # counting up and more=1 until the last.
    dut.expect("07 003500 main dev dev.frame bus=1 fmt=m5.chunk.1 bits=32 "
               "data=0001A0A1", timeout=10)
    dut.expect("16 008000 tick dev dev.frame bus=1 fmt=m5.chunk.1 bits=32 "
               "data=0900B2B3", timeout=10)

    # The narrow link: sixteen bits is the header and nothing else, so the
    # format says it cannot be carried rather than emitting a frame with
    # no payload in it.
    dut.expect("18 009500 main dev dev.note link too small for a chunk header",
               timeout=10)
    dut.expect("19 009500 main diag diag.chan_reject chan=0 len=21",
               timeout=10)

    dut.expect("20 009500 main dir dump chunk sent=2 fr=14 rx=0 len=0 no=1 "
               "bad=0", timeout=10)
    # The far end reassembled both messages to their full twenty bytes.
    dut.expect("21 009500 main dir dump chunk sent=0 fr=0 rx=2 len=20 no=0 "
               "bad=0", timeout=10)
    dut.expect("stats events=21 dropped=0 folded=0 diag=1", timeout=10)
    dut.expect("TEST done", timeout=10)
