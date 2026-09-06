"""GPIO-shaped units driven by ordinary Arduino code."""


def test_units_gpio(dut):
    dut.expect("TEST start units_gpio", timeout=10)
    # A press pulls the line low and the interrupt fires; the PIR line is
    # high while motion is fresh and low once its hold has run out; the
    # echo pulse is 600 us wide for 100 mm, exactly as the model says.
    dut.expect("values level=0 edges=1 pir_high=1 pir_low=0 echo_us=600",
               timeout=10)

    # The button: the world presses, the line falls, the sketch's handler
    # runs from the edge the environment decided.
    dut.expect("02 000000 main dir chan.write chan=0 data=01", timeout=10)
    dut.expect("03 000000 main dev gpio.inject pin=26 1->0 match=1",
               timeout=10)
    dut.expect("06 000000 main app gpio.read pin=26 val=0", timeout=10)

    # The PIR holds its line for 2500 us after the motion, which is not a
    # tick boundary: the release lands where the device asked for it.
    dut.expect("10 000000 main dev gpio.inject pin=25 0->1 match=0",
               timeout=10)
    dut.expect("12 002500 tick dev gpio.inject pin=25 1->0 match=0",
               timeout=10)

    # The relay: two writes back to back are faster than its contacts
    # settle, and a line has no return value to refuse with, so the unit
    # reports it instead.
    dut.expect("14 003500 main app gpio.write pin=19 val=1", timeout=10)
    dut.expect("16 003500 main dev dev.note switched before contacts settled",
               timeout=10)

    # The ranger: trigger, 450 us of quiet, then an echo whose width is
    # the measurement.
    dut.expect("18 003500 main app gpio.write pin=21 val=1", timeout=10)
    dut.expect("25 003950 tick dev gpio.inject pin=22 0->1 match=0",
               timeout=10)
    dut.expect("TEST done", timeout=20)
