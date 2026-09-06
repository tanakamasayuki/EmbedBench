"""Analog, I2C and binary-serial units."""


def test_units_bus(dut):
    dut.expect("TEST start units_bus", timeout=10)
    # The knob reads back as its raw count and as millivolts, the room is
    # dark enough to trip the light unit's digital output, the encoder has
    # moved three detents, and the Modbus slave answered with the register
    # the world had put there.
    dut.expect("values angle=2048 mv=1650 light=500 dark=1 count=3 reg=BEEF",
               timeout=10)

    # Analog: the unit presents both readings itself, with no director
    # step between the model and what the application reads.
    dut.expect("02 000000 main dev analog.inject pin=35 val=2048", timeout=10)
    dut.expect("03 000000 main dev analog.inject.mv pin=35 mv=1650",
               timeout=10)
    dut.expect("09 000000 main dev analog.inject pin=36 val=500", timeout=10)

    # I2C: a plain read (no repeated start) works on this part, unlike the
    # environmental sensor, and the counter comes back little endian.
    dut.expect("17 000000 main dev i2c.rd.resp len=2 data=0300 re=16",
               timeout=10)

    # Modbus: framing is silence, so the reply comes 1500 us after the
    # request rather than immediately, and it is binary (summarized).
    dut.expect("22 001500 tick dev dev.tx len=7 crc=D8", timeout=10)
    # A corrupted frame is answered with silence, which only the
    # diagnostic path can make visible.
    dut.expect("31 003000 tick dev dev.note frame dropped: bad crc",
               timeout=10)

    dut.expect("33 004500 main dir dump encoder count=3 pressed=0 "
               "led=102030 w=1", timeout=10)
    dut.expect("34 004500 main dir dump modbus answered=1 bad_crc=1 "
               "foreign=0 r0=BEEF", timeout=10)
    dut.expect("stats events=34 dropped=0 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
