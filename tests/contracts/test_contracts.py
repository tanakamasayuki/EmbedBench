"""Channel, dump, and time contracts of the device interface (native)."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE.parent.parent / "src"
MODELS = HERE.parent / "common_models" / "src"


def test_contracts():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            f"-I{SRC}", f"-I{MODELS}",
            str(HERE / "native" / "main.cpp"),
            str(MODELS / "temp_model.cpp"),
            str(MODELS / "modem_model.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    # channelWrite: false for unsupported channel / wrong length, true when
    # applied. channelRead / dump: snprintf-style needed length, partial
    # write within cap, dump NUL-terminated after truncation.
    assert ("channel bad_chan=0 bad_len=0 ok=1 cap1_need=2 b0=01 b1=EE "
            "cap2_need=2 bad_read=0 dump_need=16 dump_out=<temp=01> dump_len=7"
            ) in result.stdout
    # advanceTo: repeated time emits once, a jump delivers the due reply at
    # the jump time, reset drops pending due times.
    assert ("time first=1 repeat=1 jump_calls=2 jump_at=5000 after_reset=2"
            in result.stdout)
    assert "NATIVE done" in result.stdout
