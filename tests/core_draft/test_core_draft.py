"""Integrated draft core: one multi-bus scenario, one ordered event list."""


def test_core_draft(dut):
    dut.expect("TEST start core_draft", timeout=10)
    dut.expect("values t1=250 t2=300 spins=3", timeout=10)

    dut.expect("01 000000 main app i2c.req addr=48 data=0105", timeout=10)
    dut.expect("02 000000 main dev i2c.resp status=0 re=1", timeout=10)
    dut.expect("03 000000 main app int.attach pin=27 trig=1", timeout=10)
    dut.expect("04 000000 tick dir gpio.inject pin=27 0->1 match=1",
               timeout=10)
    dut.expect("05 000000 isr core isr.enter pin=27", timeout=10)
    dut.expect("06 000000 isr app gpio.write pin=5 val=1", timeout=10)
    dut.expect("07 000000 isr core isr.exit pin=27", timeout=10)
    dut.expect("08 000000 main app i2c.req addr=48 data=00", timeout=10)
    dut.expect("09 000000 main dev i2c.resp status=0 re=8", timeout=10)
    dut.expect("10 000000 main app i2c.rd.req addr=48 req=2", timeout=10)
    dut.expect("11 000000 main dev i2c.rd.resp len=2 data=00FA re=10",
               timeout=10)
    dut.expect("12 000000 main app uart.tx AT", timeout=10)
    dut.expect("13 000000 main dev dev.tx OK", timeout=10)
    dut.expect("14 000000 main app uart.rx O", timeout=10)
    dut.expect("15 000000 main app uart.rx K", timeout=10)
    dut.expect("16 002000 tick dir chan.write chan=0 data=012C", timeout=10)
    dut.expect("17 002000 main app i2c.req addr=48 data=00", timeout=10)
    dut.expect("18 002000 main dev i2c.resp status=0 re=17", timeout=10)
    dut.expect("19 002000 main app i2c.rd.req addr=48 req=2", timeout=10)
    dut.expect("20 002000 main dev i2c.rd.resp len=2 data=012C re=19",
               timeout=10)
    dut.expect("21 002000 main dir dump temp=012C cfg=05", timeout=10)

    dut.expect(
        "stats events=21 dropped=0 zero_waits=3 zero_in_dir=0 late_ticks=0 "
        "ticks=2 diag=0",
        timeout=10)
    dut.expect("metrics event_bytes=72 resp_lines=5", timeout=10)
    dut.expect("run2_same=1 run3_same=1", timeout=10)
    dut.expect("TEST done", timeout=10)
