"""Environment example #2: the same models on a pure native recorder."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
MODELS = HERE.parent / "common_models" / "src"

EXPECTED_TRACE = [
    "01 000000 main app i2c.req addr=48 data=0105 stop=1",
    "02 000000 main dev i2c.resp status=0 re=1",
    "03 000000 main dir chan.write chan=0 data=012C",
    "04 000000 main dev gpio.inject line=0 val=1",
    "05 000000 main app i2c.req addr=48 data=00 stop=1",
    "06 000000 main dev i2c.resp status=0 re=5",
    "07 000000 main app i2c.rd.req addr=48 req=2 stop=1",
    "08 000000 main dev i2c.rd.resp len=2 data=012C re=7",
    "09 000000 main app uart.tx AT+S",
    "10 001000 tick dev dev.tx OK",
    "11 001000 main app uart.rx O",
    "12 001000 main app uart.rx K",
    "13 001000 main dir dump temp=012C cfg=05",
    "14 001000 main dir dump modem replies=1 pending=0",
]


def loc(path: Path) -> int:
    return len([
        l for l in path.read_text().splitlines()
        if l.strip() and not l.strip().startswith("//")
    ])


def test_native_env():
    # The environment itself must be as platform-free as the models.
    for source in [HERE / "nenv.h", HERE / "nenv.cpp"]:
        includes = [
            l for l in source.read_text().splitlines()
            if l.strip().startswith("#include")
        ]
        for token in ["Arduino.h", "Host", "embedbench_draft"]:
            for line in includes:
                assert token not in line, (
                    f"{source.name} includes {token}: {line.strip()}")

    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_env"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{MODELS}",
            str(HERE / "native" / "main.cpp"),
            str(HERE / "nenv.cpp"),
            str(MODELS / "temp_model.cpp"),
            str(MODELS / "modem_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    out = result.stdout
    assert "values t1=300 reply=OK elapsed=1000" in out
    for line in EXPECTED_TRACE:
        assert line in out, f"missing trace line: {line}"
    assert "stats events=14 dropped=0" in out
    assert "run2_same=1 run3_same=1" in out
    assert "NATIVE done" in out

    sizes = {"env": loc(HERE / "nenv.h") + loc(HERE / "nenv.cpp")}
    print(f"LOC {sizes}")
    # Environment example #2 in full: HostPort, app-side bus API, director
    # path, tick clock, and trace recorder (X29: 290). +38 for the contract
    # revision: I2C transaction context, schema-checked formats, frame
    # padding/atomicity checks, channel rejection diagnostics (X30-X34).
    assert sizes == {"env": 328}
