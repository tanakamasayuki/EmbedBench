"""Vertical slice: injection -> edge decision -> ISR -> ctx=isr tagging."""


def test_interrupt_slice(dut):
    dut.expect("TEST start interrupt_slice", timeout=10)
    dut.expect("01 main app gpio.write pin=4 val=1", timeout=10)
    dut.expect("02 main dir inject pin=27 0->1 match=1", timeout=10)
    dut.expect("03 isr core isr.enter pin=27", timeout=10)
    dut.expect("04 isr app gpio.write pin=5 val=1", timeout=10)
    dut.expect("05 isr core isr.exit pin=27", timeout=10)
    dut.expect("06 main dir inject pin=27 1->0 match=0", timeout=10)
    dut.expect("07 main app gpio.write pin=4 val=0", timeout=10)
    dut.expect("fires=1 isr_tagged=3 events=7", timeout=10)
    dut.expect("TEST done", timeout=10)
