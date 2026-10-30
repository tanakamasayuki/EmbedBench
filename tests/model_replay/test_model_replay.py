"""X68: models built from recordings, checked against recordings.

QMP6988 and SHT30 (M5Stack U001-C) are modelled from the I2CDeviceDB pilot
and then checked, with devices/tools/tracecmp.py, against it and against
the first OEP runs (EmbedBenchDeviceData): the characterization of each
part and the M5Unit-ENV library driving both. Every request is made at its
recorded time; what differs is what the model answered. Measured values
are the world's, so QMP6988's data registers are compared by whether they
changed, and SHT30's samples are fed in.
"""

import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
TOOL = ROOT / "devices" / "tools" / "tracecmp.py"
MODELS = ROOT / "devices" / "src"
PILOT_SHT30 = ROOT / "tests" / "tape_replay" / "sht30_u001c.trace"

# 32 distinct raw samples: values say nothing, only that a sample is new.
QMP_SAMPLES = "".join("%02X0000%02X0000" % (0x80 + i, 0x40 + i) for i in range(32))
QMP = ["--addr", "70", "--model", "qmp6988_model.h:Qmp6988Model",
       "--source", str(MODELS / "qmp6988_model.cpp"),
       "--source", str(MODELS / "regtable_model.cpp"),
       "--channel", "0", QMP_SAMPLES, "--changes", "F7-FC"]
# The OEP run's controller (ESP32-P4) gave up on a stretched SCL after
# 6.7 ms (the read's own report); the pilot's ESP32-S3 at about the same.
STRETCH_LIMIT = ["--channel", "1", "00001A2C"]


def sht30(samples):
    return ["--addr", "44", "--model", "sht30_model.h:Sht30Model",
            "--source", str(MODELS / "sht30_model.cpp"), "--channel", "0", samples]


def tracecmp(trace, args, extra=()):
    result = subprocess.run([sys.executable, str(TOOL), str(trace), *args, *extra],
                            capture_output=True, text=True)
    lines = result.stdout.splitlines()
    summary = re.search(r"(\d+) transfers compared, (\d+) differ", result.stdout)
    assert summary, result.stdout + result.stderr
    diffs = [l for l in lines if ": recorded " in l]
    return int(summary.group(1)), diffs


def words(trace, *, skip_prefix=None):
    """The (temperature, humidity) words of each six-byte SHT30 answer."""
    out = []
    for data in re.findall(r"i2c\.rd\.resp len=6 data=([0-9A-F]{12})", trace.read_text()):
        if skip_prefix and data.startswith(skip_prefix):
            continue
        out.append(data[0:4] + data[6:10])
    return "".join(out)


def test_qmp6988_matches_every_recording():
    # The pilot (forced, normal), the OEP characterization (forced at P x8,
    # IIR, normal read every 10 ms, a 3-byte read from F3) and M5Unit-ENV
    # (calibration byte by byte after a STOP, F7 then FA). At 400 kHz the
    # forced conversion is still running when normal mode is written, and
    # every sample lands on its schedule. A sweep reads status 20 us apart
    # from 183 to 773 us after a forced write: the bit rises between 216 and
    # 234 us. The address 0x73 transfer of a
    # reset is left out (--only-addr; see the last test).
    for name, count in (("qmp6988_u001c.trace", 57), ("qmp6988_modes_p4x.trace", 64),
                        ("qmp6988_modes_p4x_400k.trace", 44),
                        ("qmp6988_measure_bit_p4x_400k.trace", 94),
                        ("m5unit_env_basic_p4x.trace", 80)):
        compared, diffs = tracecmp(HERE / name, QMP, ["--only-addr"])
        assert (compared, diffs) == (count, []), (name, diffs)


def test_qmp6988_timing_is_what_is_checked(tmp_path):
    # Start the pilot's forced conversion 200 us later and keep every poll
    # where it was: the poll that saw the conversion finished (190 us after
    # the model's 4.65 ms) now finds the model still converting. A late or
    # early model shows as an answer.
    text = (HERE / "qmp6988_u001c.trace").read_text()
    shifted = tmp_path / "late.trace"
    shifted.write_text(text.replace("55602 i2c.req addr=70 data=F425 stop=1",
                                    "55802 i2c.req addr=70 data=F425 stop=1"))
    _, diffs = tracecmp(shifted, QMP, ["--only-addr"])
    assert diffs == ["60442 i2c.rd.req addr=70 req=1 stop=1 rs: recorded len=1 data=00, "
                     "model len=1 data=08"], diffs


def test_sht30_matches_with_two_known_differences():
    # The library run: three high-repeatability stretched measurements read
    # 200 ms later. Nothing differs.
    run = HERE / "m5unit_env_basic_p4x.trace"
    assert tracecmp(run, sht30(words(run, skip_prefix="A2")), [*STRETCH_LIMIT, "--only-addr"]) \
        == (7, [])

    # The OEP characterization: soft reset, status, three repeatabilities by
    # polling and by stretching. Every poll is refused or answered when the
    # part did. The one difference: the controller gave up on the high
    # stretch after one byte had crossed the bus (the library got 0); the
    # interface has no partly-answered read. The failed measurement takes a
    # sample of its own.
    run = HERE / "sht30_modes_p4x.trace"
    samples = "62CA9F41" + "00000000" + "62D59F2362EF9F3A62DA9F6262D09F28"
    compared, diffs = tracecmp(run, sht30(samples), [*STRETCH_LIMIT, "--only-addr"])
    assert compared == 45
    assert diffs == ["97650048 i2c.rd.req addr=44 req=1 stop=0: recorded len=1 data=62, "
                     "model len=0 data="], diffs

    # The same at 400 kHz: the controller gave up at the same point.
    run = HERE / "sht30_modes_p4x_400k.trace"
    answers = words(run)
    compared, diffs = tracecmp(run, sht30(answers[:8] + "00000000" + answers[8:]),
                               [*STRETCH_LIMIT, "--only-addr"])
    assert compared == 49
    assert diffs == ["10420851 i2c.rd.req addr=44 req=1 stop=0: recorded len=1 data=63, "
                     "model len=0 data="], diffs

    # The pilot: the same, except that it read status 0000 after a soft
    # reset where the OEP run (and the datasheet) has 8010.
    compared, diffs = tracecmp(PILOT_SHT30, sht30(words(PILOT_SHT30)), STRETCH_LIMIT)
    assert compared == 54
    assert diffs == ["10359 i2c.rd.req addr=44 req=3 stop=1: recorded len=3 data=000081, "
                     "model len=3 data=8010E1"], diffs


def test_sht30_stretch_limit_belongs_to_the_controller():
    # Without the controller's limit the model answers the pilot's high
    # stretch at once, as a patient master would see it, and the read that
    # the S3 made 24 ms later finds nothing left.
    _, diffs = tracecmp(PILOT_SHT30, sht30(words(PILOT_SHT30)))
    assert [d.split(":")[0] for d in diffs] == [
        "10359 i2c.rd.req addr=44 req=3 stop=1",
        "64340 i2c.rd.req addr=44 req=0 stop=0",
        "88369 i2c.rd.req addr=44 req=6 stop=1 rs",
    ], diffs


def test_qmp6988_reset_byte_decodes_as_an_address():
    # E0 = E6 resets the part while the E6 byte is still on the bus, and the
    # decoder reads that byte as a new address: E6 is 0x73 written. The
    # pilot (sigrok) kept E6 as data and then saw an ACKed 0x73; the OEP run
    # (wireskein) ends the write at E0 and sees a refused 0x73. Neither is
    # a part at 0x73 (X67's open question).
    pilot = (HERE / "qmp6988_u001c.trace").read_text()
    assert "7876 i2c.req addr=70 data=E0E6 stop=1\n7876 i2c.resp status=0\n" \
           "8163 i2c.req addr=73 data= stop=1\n8163 i2c.resp status=0" in pilot
    oep = (HERE / "qmp6988_modes_p4x.trace").read_text()
    assert re.search(r"99746374 i2c.req addr=70 data=E0 stop=1\n.*\n"
                     r"99746662 i2c.req addr=73 data= stop=1\n99746662 i2c.resp status=2", oep)
    assert 0xE6 >> 1 == 0x73
