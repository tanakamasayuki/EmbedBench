"""Fixed-capacity policies: what a test sees when the recorder fills up."""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent


def test_capacity_policy():
    out_dir = HERE / "output"
    out_dir.mkdir(exist_ok=True)
    binary = out_dir / "native_check"
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            str(HERE / "native" / "main.cpp"),
            "-o", str(binary),
        ],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            check=True)
    out = result.stdout
    assert "NATIVE start capacity_policy" in out
    # 46 events offered into 24 slots: a short setup, a poll loop, and the
    # conclusion the test would assert on.
    assert "scene events=46 slots=24" in out

    # Keeping the first events loses the conclusion, and nothing in the
    # trace itself says so — the count lives beside it, not in it.
    assert ("drop_new stored=24 first=1 last=24 lost=22 folded=0 setup=1 "
            "tail=0 visible=0") in out
    # A ring keeps the conclusion and loses the setup instead. The gap is
    # visible only because the first stored sequence is no longer 1.
    assert ("drop_old stored=24 first=23 last=46 lost=22 folded=0 setup=0 "
            "tail=1 visible=1") in out
    # Spending a slot on a notice makes the loss visible but costs one
    # more event and still loses the conclusion.
    assert ("drop_marked stored=24 first=1 last=46 lost=23 folded=0 setup=1 "
            "tail=0 visible=1") in out
    # Folding the repeating cycle is the only policy that keeps both ends:
    # 24 of the 46 events become repeat counts and nothing is discarded.
    assert ("compact stored=22 first=1 last=46 lost=0 folded=24 setup=1 "
            "tail=1 visible=1") in out

    # The property that bounds the blast radius: a run that fits is never
    # touched, so traces that already pass do not move.
    assert "compact_fits stored=8 lost=0 folded=0" in out

    # With nothing to fold the policy must still degrade visibly rather
    # than end without explanation. The beginning is kept (first=1) and
    # the notice states the loss; setup/tail here read 0 only because this
    # scenario contains neither probe string.
    assert ("compact_nocycle stored=24 first=1 last=46 lost=22 folded=0 "
            "setup=0 tail=0 visible=1") in out

    # A burst of 200 transfers whose every byte differs: the exact-cycle
    # tier has nothing to work with and cuts most of the run away.
    assert "burst events=409" in out
    assert ("burst_cycle_only stored=24 first=1 last=409 lost=385 folded=0 "
            "setup=1 tail=0 visible=1") in out
    # Comparing shapes instead of exact text recognises the repeating
    # request/response pair, so nothing is lost and the end of the run
    # survives. The setup folds too here (setup=0): under this much
    # pressure its numbered steps are a cycle as well, and a summarised
    # setup beats a discarded conclusion.
    assert ("burst_shapes stored=22 first=1 last=409 lost=0 folded=387 "
            "setup=0 tail=1 visible=1") in out

    # Wake slots: distinct moments beyond the table are refused, and the
    # refusal is returned to the device rather than accepted and dropped.
    assert "wake distinct=12 accepted=8 refused=4 high=8" in out
    # Devices waiting for the same instant share one slot, so a broadcast
    # answered by many devices costs one slot, not one per device — which
    # is why eight slots covers far more than eight devices.
    assert "wake same=12 accepted=12 refused=0 high=1" in out
    assert "NATIVE done" in out
