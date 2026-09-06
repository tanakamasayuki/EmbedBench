"""Self-driven units: an IMU FIFO and an RTC that keeps its own clock."""


def test_units_sense(dut):
    dut.expect("TEST start units_sense", timeout=10)

    # The watermark was reached on the device's own schedule, and the
    # burst read returned what was actually there (8 samples, 16 bytes)
    # rather than the 40 bytes the sketch offered to hold. Draining past
    # the watermark took the interrupt line back down.
    dut.expect("values count=8 flags=0 bytes=16 sample=1234 irq=0",
               timeout=10)
    # Away for 60 ms with 40 ms of capacity: the FIFO is full and the
    # sticky overflow flag is set, so the loss cannot pass unnoticed.
    dut.expect("values late_count=16 late_flags=1", timeout=10)
    # The RTC's clock advanced two seconds of wall time, the alarm fired,
    # and an alarm set in the past was refused with a data NACK (3).
    dut.expect("values rtc_start=1000 rtc_end=1002 int=1 fired=1 past=3",
               timeout=10)

    # 8 samples at 2.5 ms is 20,000 us — the device's schedule, not the
    # environment's 10 ms tick.
    dut.expect("04 020000 tick dev gpio.inject pin=33 0->1 match=0",
               timeout=10)
    dut.expect("08 021500 main dev i2c.rd.resp len=2 data=0800 re=7",
               timeout=10)
    # The line comes down inside the read that drained the FIFO, and is
    # recorded before the data it caused.
    dut.expect("12 021500 main dev gpio.inject pin=33 1->0 match=0",
               timeout=10)
    dut.expect("13 021500 main dev i2c.rd.resp len=16 data=1234123412 re=11",
               timeout=10)
    # The 17th sample after the drain has nowhere to go: 021500 plus
    # 16 slots of 2.5 ms is 061500, so the next one at 062500 overflows.
    dut.expect("16 062500 tick dev dev.note fifo overflow: samples lost",
               timeout=10)
    dut.expect("20 081500 main dev i2c.rd.resp len=2 data=1001 re=19",
               timeout=10)

    # An alarm already in the past is refused rather than left to never
    # fire: the note explains it and the status is a data NACK.
    dut.expect("30 081500 main dev dev.note alarm time already past",
               timeout=10)
    dut.expect("31 081500 main dev i2c.resp status=3 re=29", timeout=10)
    # Two seconds of wall time later to the microsecond, on a 10 ms tick:
    # the device converted its own unit into a wake the environment could
    # honour.
    dut.expect("34 2081500 tick dev gpio.inject pin=34 0->1 match=0",
               timeout=10)

    dut.expect("44 2181500 main dir dump imu on=0 count=16 taken=8 lost=8 "
               "irq=1", timeout=10)
    dut.expect("45 2181500 main dir dump rtc sec=1002 alarm=1002 fired=1 "
               "rejected=1", timeout=10)
    dut.expect("stats events=45 dropped=0 folded=0 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
