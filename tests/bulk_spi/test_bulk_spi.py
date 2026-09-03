"""Bulk SPI granularity: transaction summary vs per-byte pair explosion."""


def test_bulk_spi(dut):
    dut.expect("TEST start bulk_spi", timeout=10)
    # 210 record attempts total (2 transaction + 8 per-byte + 200 burst);
    # the 64-slot buffer keeps 64 and counts 146 dropped — the explosion
    # the transaction summary avoids.
    dut.expect("stats events=64 dropped=146", timeout=10)
    # 256 bytes inside the transaction became exactly two lines.
    dut.expect("01 000000 main app spi.begin", timeout=10)
    dut.expect("02 000000 main app spi.bulk n=256 mosi_sum=80 miso_sum=80",
               timeout=10)
    # Outside a transaction the per-byte pair remains.
    dut.expect("03 000000 main app spi.req mosi=A0", timeout=10)
    dut.expect("04 000000 main dev spi.resp miso=5F re=3", timeout=10)
    dut.expect("05 000000 main app spi.req mosi=A1", timeout=10)
    dut.expect("06 000000 main dev spi.resp miso=5E re=5", timeout=10)
    dut.expect("run2_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
