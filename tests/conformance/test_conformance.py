"""One standard scenario, two environments, one verdict."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
MODELS = HERE.parent / "common_models" / "src"
ENV = HERE.parent / "common_env"

# checks() is a bitmask of the contracts the probe managed to verify. The
# required set is every bit except "advanceTo repeated a time", which the
# contract allows but does not demand — neither environment repeats one in
# this scenario, so both report 1FB and still conform.
OBSERVED_CHECKS = "1FB"


def test_native_conformance():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{MODELS}", f"-I{ENV}",
            str(HERE / "native" / "main.cpp"),
            str(ENV / "nenv.cpp"),
            str(MODELS / "conformance_probe.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    assert (f"conformance ok=1 checks={OBSERVED_CHECKS} violations=0"
            in result.stdout), result.stdout
    # And the kit is not vacuous: an environment that re-enters the device,
    # lets time run backwards, and truncates an oversized frame is caught.
    # Six violations: time going backwards once, an oversized frame
    # accepted once, one re-entry, and a clock reading behind the last
    # advanceTo on each of the three calls that looked.
    assert "broken ok=0 violations=6" in result.stdout, result.stdout
    assert "NATIVE done" in result.stdout


def test_host_conformance(dut):
    dut.expect("TEST start conformance", timeout=10)
    dut.expect(f"conformance ok=1 checks={OBSERVED_CHECKS} violations=0",
               timeout=10)
    # The host environment also reports, from its own side, that it never
    # re-entered the device during the scenario.
    dut.expect("device_depth=1", timeout=10)
    dut.expect("TEST done", timeout=10)
