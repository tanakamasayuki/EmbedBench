"""Radio-shaped units on the generic frame path (IR, LoRa, UWB anchors)."""


def test_units_radio(dut):
    dut.expect("TEST start units_radio", timeout=10)

    # IR: a held button produced one code and two repeats, and the
    # application collapsed them back into a single press. The unit also
    # heard another remote in the room.
    dut.expect("values presses=1 repeats=2 cmd=12 rx=4099", timeout=10)
    # LoRa: four outcomes for four sends. The transport accepted the first
    # three; the modem itself kept only the first.
    dut.expect("values tx1=1 tx2=1 six=1 twelve=0 busy=0 done=1", timeout=10)
    # The downlink payload carries three bytes; its signal quality is not
    # in the payload and came back through a channel read instead.
    dut.expect("values dn_len=3 dn=010203 rssi=-97 snr=8 st=3", timeout=10)
    # Three anchors, one broadcast poll, answers in anchor-id order.
    dut.expect("values ranges=200,1000,3000 order=012 n=3", timeout=10)

    # A repeat carries no payload at all: NEC's repeat code is an empty
    # frame, which the interface allows and the log prints as such.
    dut.expect("02 000000 main dev dev.frame bus=0 fmt=m5.ir.nec.1 bits=16 "
               "data=4012", timeout=10)
    dut.expect("03 002000 tick dev dev.frame bus=0 fmt=m5.ir.rep.1 bits=0 "
               "empty", timeout=10)
    dut.expect("04 004000 tick dev dev.frame bus=0 fmt=m5.ir.rep.1 bits=0 "
               "empty", timeout=10)

    # LoRa air time is the payload's, not a constant: four bytes hold the
    # line low from 5,000 us to 9,000 us, and a send inside that window is
    # dropped by the modem while the transport reports success.
    dut.expect("07 005000 main dev gpio.inject pin=12 0->0", timeout=10)
    dut.expect("09 005000 main app frame.tx bus=1 fmt=m5.lora.up.1 bits=8 "
               "data=DE", timeout=10)
    dut.expect("10 005000 main dev dev.note send while transmitting",
               timeout=10)
    dut.expect("11 009000 tick dev gpio.inject pin=12 0->1", timeout=10)

    # Two different limits, two different refusals: six bytes reach the
    # modem and are refused by it; twelve never leave the transport.
    dut.expect("13 010000 main app frame.tx bus=1 fmt=m5.lora.up.1 bits=48",
               timeout=10)
    dut.expect("14 010000 main dev dev.note payload over radio limit",
               timeout=10)
    dut.expect("15 010000 main diag diag.frame_oversize bus=1 bits=96 max=64",
               timeout=10)

    # A 12-bit poll: four tag bits and eight sequence bits, MSB-first with
    # the last byte's low nibble cleared (5A30 = tag 5, seq A3).
    dut.expect("21 010000 main app frame.tx bus=2 fmt=m5.uwb.poll.1 bits=12 "
               "data=5A30", timeout=10)
    # All three anchors object to the second poll: the round is still open.
    dut.expect("23 010000 main dev dev.note poll during ranging", timeout=10)
    dut.expect("24 010000 main dev dev.note poll during ranging", timeout=10)
    dut.expect("25 010000 main dev dev.note poll during ranging", timeout=10)

    # Each anchor answers in its own slot: 500 us plus 300 us per id. None
    # of these land on a 1,000 us tick, so all three depend on the
    # environment holding more than one outstanding wake at a time.
    dut.expect("26 010500 tick dev dev.frame bus=2 fmt=m5.uwb.resp.1 bits=24 "
               "data=0000C8", timeout=10)
    dut.expect("27 010800 tick dev dev.frame bus=2 fmt=m5.uwb.resp.1 bits=24 "
               "data=0103E8", timeout=10)
    dut.expect("28 011100 tick dev dev.frame bus=2 fmt=m5.uwb.resp.1 bits=24 "
               "data=020BB8", timeout=10)

    dut.expect("29 013100 main dir dump ir sent=3 rx=1 last=4099 held=0",
               timeout=10)
    dut.expect("30 013100 main dir dump lora sent=1 dropped=2 rx=1 rssi=-97 "
               "busy=0", timeout=10)
    dut.expect("31 013100 main dir dump uwb id=2 mm=3000 tag=5 seq=163 "
               "answered=1 coll=1", timeout=10)

    dut.expect("stats events=31 dropped=0 diag=1", timeout=10)
    dut.expect("TEST done", timeout=10)
