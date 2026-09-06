"""A sequence-locked SPI flash and a codec living on two buses."""

import re


def test_units_mixed(dut):
    dut.expect("TEST start units_mixed", timeout=10)

    # The status register tells the story of the sequence: idle, then
    # write-enabled after WREN (bit 1), then busy during the program
    # (bit 0). The program that arrived without WREN left the byte
    # erased; the one that followed the sequence wrote 0xBE.
    dut.expect("values sr0=00 sr_wren=02 sr_busy=01 no_wren=FF after=BE",
               timeout=10)
    # One part, two buses: the volume written over I2C scales what the
    # SPI path returns, mute silences it, and the control bus can report
    # how many samples the data bus saw.
    dut.expect("values loud=80 quiet=20 muted=00 count=3", timeout=10)

    # Chip select frames the command the way STOP frames an I2C transfer:
    # the read status command is two bytes between one low and one high.
    dut.expect("01 000000 main app gpio.write pin=5 val=0", timeout=10)
    dut.expect("02 000000 main app spi.req mosi=05", timeout=10)
    dut.expect("05 000000 main dev spi.resp miso=00 re=4", timeout=10)
    dut.expect("06 000000 main app gpio.write pin=5 val=1", timeout=10)

    # Byte-per-transfer SPI fills the buffer, so the repeats fold (X53,
    # X54) rather than the end of the run being lost. Escaped literally:
    # the shape placeholder and the span separator are regex characters.
    dut.expect(re.escape("08 000000 main app spi.req mosi=* crc=96 "
                         "x4..000000"), timeout=10)

    # The mistake worth catching: a page program with no write-enable in
    # front of it. A real part discards it without a word; this one says
    # so as the line is released, which is when the command commits.
    dut.expect("17 000000 main dev dev.note program without write-enable",
               timeout=10)

    # Done in the right order the status reads back write-enabled.
    dut.expect("27 000000 main app spi.req mosi=06", timeout=10)
    dut.expect("34 000000 main dev spi.resp miso=02 re=33", timeout=10)
    # Then the page program itself: command, address, two data bytes.
    dut.expect("37 000000 main app spi.req mosi=02", timeout=10)

    # The codec's two paths interleave on one part. Full volume passes
    # the sample through; a quarter volume written over I2C between two
    # identical SPI transfers changes what the second one returns.
    dut.expect("62 004000 main dev spi.resp miso=80 re=61", timeout=10)
    dut.expect("64 004000 main app i2c.req addr=1A data=0040 stop=1",
               timeout=10)
    dut.expect("68 004000 main dev spi.resp miso=20 re=67", timeout=10)
    dut.expect("70 004000 main app i2c.req addr=1A data=0101 stop=1",
               timeout=10)
    dut.expect("74 004000 main dev spi.resp miso=00 re=73", timeout=10)
    # The count register: the control bus reporting the data bus's work.
    dut.expect("79 004000 main dev i2c.rd.resp len=1 data=03 re=78",
               timeout=10)

    dut.expect("80 004000 main dir dump flash we=0 busy=0 progs=1 no=1 "
               "m0=BE m1=EF", timeout=10)
    dut.expect("81 004000 main dir dump codec vol=64 mute=1 n=3 last=00 "
               "stray=0", timeout=10)
    dut.expect("stats events=59 dropped=0 folded=22 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
