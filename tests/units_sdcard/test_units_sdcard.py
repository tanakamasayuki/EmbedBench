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

    # Four volumes that are wrong in ways the boot signature cannot show.
    # Zero bytes-per-sector is refused because the divisors are checked
    # before they are used. The healthy volume reads its 48 bytes through
    # the same chain follower (0 = complete). Then the three that matter:
    # a chain that loops (1), one leading off the end of the volume (2),
    # and one that ends before the directory's size is satisfied (3).
    # Reporting the loop rather than hanging on it is the whole reason
    # the reader keeps a visited-cluster set.
    dut.expect("values zerobps=0 ok=0,48 loop=1 range=2 short=3", timeout=10)

    # A 512-byte block is 515 SPI transfers, so the per-byte pairs
    # dominate everything else. Folding by shape (X54) carries them.
    dut.expect("01 000000 main app gpio.write pin=5 val=0", timeout=10)

    # Twenty-five block reads and one write across seven volumes, and the
    # trace still reaches the end of the run.
    dut.expect("27375 004000 main dir dump sd init=1 blk=3 rd=25 wr=1 no=0 "
               "b0=456D", timeout=10)
    # The headline number: 27,375 events offered, 61 kept, 27,314 folded
    # into repeat counts, and nothing dropped. This is by far the largest
    # load the recording policy has carried, and it lost nothing.
    dut.expect("stats events=61 dropped=0 folded=27314 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
