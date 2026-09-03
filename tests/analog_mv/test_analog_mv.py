"""Verify the 1.7.1 analog mV / read-config hooks requested as H3."""


def test_analog_mv(dut):
    dut.expect("TEST start analog_mv", timeout=10)
    dut.expect("baseline mv=3300", timeout=10)
    dut.expect("hooked mv=1650 held=3300 mv_calls=1 raw_calls=0", timeout=10)
    dut.expect("raw value=1000 raw_calls=1 mv_calls=1", timeout=10)
    dut.expect("config bits0=9 bits1=11 calls=2", timeout=10)
    dut.expect("cleared mv=3300 mv_calls=1 config_calls=2", timeout=10)
    dut.expect("TEST done", timeout=10)
