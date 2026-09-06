"""A block device under a real FAT12 reader."""

import re


def test_units_sdcard(dut):
    dut.expect("TEST start units_sdcard", timeout=10)

    # The file system is the code under test, not the model. It mounted a
    # real FAT12 volume off numbered blocks, found the directory entry,
    # read its true size out of it, and pulled the file's first cluster.
    dut.expect("values mount=1 found=1 size=48 head=EmbedBenc", timeout=10)
    # Then it wrote the cluster back and read it again: 0x52 is the 'R' of
    # REWRITTEN, so the write really reached the card's store and survived
    # the 4 ms the card spent busy.
    # The other two presets: a formatted volume with no files on it, and
    # one whose boot signature was erased, which the reader refuses.
    dut.expect("values wrote=1 reread=52 empty=0 bad=0", timeout=10)

    # A 512-byte block is 515 SPI transfers, so the per-byte pairs
    # dominate everything else. Folding by shape (X54) carries them: the
    # 454 transfers of one block read become two lines with a checksum.
    dut.expect("01 000000 main app gpio.write pin=5 val=0", timeout=10)
    dut.expect(re.escape("154 000000 main app spi.req mosi=* crc=9A "
                         "x454..000000"), timeout=10)
    dut.expect(re.escape("155 000000 main dev spi.resp miso=* crc=CD re=154 "
                         "x454..000000"), timeout=10)

    # Seven block reads and one write, and the volume still starts with
    # the FAT12 jump instruction.
    dut.expect("8547 004000 main dir dump sd init=1 blk=0 rd=7 wr=1 no=0 "
               "b0=EB3C", timeout=10)
    # The headline number: 8,547 events offered, 57 kept, 8,490 folded
    # into repeat counts, and nothing dropped. This is the largest load
    # the recording policy has been put under, and it lost nothing.
    dut.expect("stats events=57 dropped=0 folded=8490 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
