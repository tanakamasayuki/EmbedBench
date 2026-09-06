"""Bulk SPI granularity: transaction summary vs per-byte pair explosion."""

import re


def test_bulk_spi(dut):
    dut.expect("TEST start bulk_spi", timeout=10)
    # 210 record attempts total (2 transaction + 8 per-byte + 200 burst).
    # Every byte of the burst differs, so there is no exact cycle to fold
    # (X53); the shape summary (X54) recognises the repeating request /
    # response pair anyway and nothing is lost. The transaction summary
    # is still the better answer: it costs one line at the moment of the
    # call, while the per-byte path only collapses once the buffer fills.
    dut.expect("stats events=36 dropped=0", timeout=10)
    # 256 bytes inside the transaction became exactly two lines.
    dut.expect("01 000000 main app spi.begin", timeout=10)
    # The summary carries CRC-8 rather than a byte sum: a sum cannot see a
    # reordering, and bulk payloads are full of them (tests/bulk_checksum).
    dut.expect("02 000000 main app spi.bulk n=256 mosi_crc=14 miso_crc=30",
               timeout=10)
    # Under pressure the pair folds by shape: the values are given up but
    # the count and a checksum over them remain, so a test can still say
    # how many transfers happened and that the data was right. The link
    # (re=) survives the fold, so the pairing is still readable.
    # Escaped literally: the shape placeholder and the span separator are
    # regex characters.
    dut.expect(re.escape("03 000000 main app spi.req mosi=* crc=F3 "
                         "x31..000000"), timeout=10)
    dut.expect(re.escape("04 000000 main dev spi.resp miso=* crc=20 re=3 "
                         "x31..000000"), timeout=10)
    # Outside a transaction the per-byte pair is recorded individually,
    # and stays that way while there is room: the last two of the burst
    # are still verbatim.
    dut.expect("183 000000 main app spi.req mosi=56", timeout=10)
    dut.expect("184 000000 main dev spi.resp miso=A9 re=183", timeout=10)
    # The trace now reaches the end of the run rather than stopping at a
    # truncation notice: the last line is the last thing that happened.
    dut.expect("last 210 000000 main dev spi.resp miso=9C re=209", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
