"""Bulk SPI granularity: transaction summary vs per-byte pair explosion."""


def test_bulk_spi(dut):
    dut.expect("TEST start bulk_spi", timeout=10)
    # 210 record attempts total (2 transaction + 8 per-byte + 200 burst);
    # the 64-slot buffer keeps 64 and counts 147 dropped — the explosion
    # the transaction summary avoids. Every byte of the burst differs, so
    # there is no cycle to fold (X53) and the buffer really does have to
    # cut; the extra one over the 146 offered beyond capacity is the slot
    # given to the notice.
    dut.expect("stats events=64 dropped=147", timeout=10)
    # 256 bytes inside the transaction became exactly two lines.
    dut.expect("01 000000 main app spi.begin", timeout=10)
    # The summary carries CRC-8 rather than a byte sum: a sum cannot see a
    # reordering, and bulk payloads are full of them (tests/bulk_checksum).
    dut.expect("02 000000 main app spi.bulk n=256 mosi_crc=14 miso_crc=30",
               timeout=10)
    # Outside a transaction the per-byte pair remains.
    dut.expect("03 000000 main app spi.req mosi=A0", timeout=10)
    dut.expect("04 000000 main dev spi.resp miso=5F re=3", timeout=10)
    dut.expect("05 000000 main app spi.req mosi=A1", timeout=10)
    dut.expect("06 000000 main dev spi.resp miso=5E re=5", timeout=10)
    # The trace ends by saying it was cut, so a reader never mistakes the
    # last recorded event for the last thing that happened. The notice
    # carries the sequence the run actually reached (210), so the jump
    # from the last kept event shows the width of the gap.
    dut.expect("last 210 000000 main diag trace.truncated lost=147",
               timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
