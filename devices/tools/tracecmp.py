#!/usr/bin/env python3
"""Check a device model against a recording of the real part.

The recording's requests are played into the model at their recorded
times (linereplay_main.cpp, built here with the model), and the model's
answers are compared with the part's, transfer by transfer:

    tracecmp.py recorded.trace --addr 70 \
        --model qmp6988_model.h:Qmp6988Model \
        --source devices/src/qmp6988_model.cpp \
        --source devices/src/regtable_model.cpp \
        [--channel 0 <hex>] [--mask F7] [--only-addr]

What is compared, and how:

- every answer to the model's address: status, length and bytes;
- `--mask <reg>` compares only the length of reads that follow a pointer
  write of <reg> (hex): measured values belong to the world, not the model;
- `--changes <reg>[-<reg>]` compares, for reads from a pointer in the range,
  whether the value changed since the previous read from that pointer
  rather than the value itself: when a part
  produces a new sample, without saying what it measured;
- `--only-addr` leaves out transfers to other addresses (a recording of a
  shared bus has other parts on it; without it they must have been absent);
- a read the part refused (recorded with req=0) is asked of the model at
  the length of the next answered read, so a model that answers too early
  shows it;
- times are not compared: each request is made at its recorded time, so
  a model that is late or early shows up as a different answer (a status
  bit still set, a value not yet updated), which is where it matters.

Prints one line per difference, `<time> <request>: recorded <x>, model <y>`,
and a summary; exits 1 when there is any difference.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MAIN = HERE / "linereplay_main.cpp"

REQ_RE = re.compile(r"^(\d+) (i2c\.req|i2c\.rd\.req) (.*)$")
RESP_RE = re.compile(r"^(\d+) (i2c\.resp|i2c\.rd\.resp) (.*)$")


def fields(text):
    return dict(f.split("=", 1) for f in text.split() if "=" in f)


def transfers(lines):
    """(time, request kind, request fields, response fields) per transfer."""
    out = []
    pending = None
    for line in lines:
        m = REQ_RE.match(line)
        if m:
            pending = (int(m.group(1)), m.group(2), fields(m.group(3)), line.split(" ", 1)[1])
            continue
        m = RESP_RE.match(line)
        if m and pending is not None:
            out.append(pending + (fields(m.group(3)),))
            pending = None
    return out


def answer(kind, resp):
    # A read the part did not answer is `len=0`, whether its address was
    # refused (status=2) or acknowledged with nothing: the frozen interface
    # has one way to say "nothing", so the status of a read is not compared.
    if kind == "i2c.req":
        return "status=%s" % resp.get("status", "0")
    return "len=%s data=%s" % (resp.get("len"), resp.get("data", ""))


def ask_full_length(lines):
    """A refused read is recorded with req=0: no byte was transferred, so
    the length the master wanted is lost. Asking a model for 0 bytes would
    hide whether it would have answered, so such a read asks for the length
    of the next answered read of the same address (1 when there is none)."""
    out = list(lines)
    for i, line in enumerate(out):
        m = REQ_RE.match(line)
        if not m or m.group(2) != "i2c.rd.req":
            continue
        req = fields(m.group(3))
        if req.get("req") != "0":
            continue
        want = "1"
        for later in out[i + 1:]:
            n = REQ_RE.match(later)
            if n and n.group(2) == "i2c.rd.req":
                f = fields(n.group(3))
                if f.get("addr") == req.get("addr") and f.get("req") != "0":
                    want = f["req"]
                    break
        out[i] = line.replace(" req=0 ", " req=%s " % want, 1)
    return out


def compare(recorded, modelled, addr, masks=(), only_addr=False, changes=()):
    """Differences between the part's answers and the model's."""
    diffs = []
    rec, mod = transfers(recorded), transfers(modelled)
    if len(rec) != len(mod):
        return ["transfer count: recorded %d, model %d" % (len(rec), len(mod))]
    pointer = None
    compared = 0
    last = {}  # (side, register) -> the data its previous read returned
    for (t, kind, req, text, want), (_, _, _, _, got) in zip(rec, mod):
        a = int(req["addr"], 16)
        if a != addr and only_addr:
            continue
        if a == addr and kind == "i2c.req" and req.get("data"):
            pointer = req["data"][:2].upper()
        expected, actual = answer(kind, want), answer(kind, got)
        if a == addr and kind == "i2c.rd.req" and pointer in masks:
            expected = "len=%s" % want.get("len")
            actual = "len=%s" % got.get("len")
        if a == addr and kind == "i2c.rd.req" and pointer is not None and \
                any(lo <= int(pointer, 16) <= hi for lo, hi in changes):
            marks = []
            for side, resp in (("rec", want), ("mod", got)):
                before = last.get((side, pointer))
                now = resp.get("data", "")
                marks.append("len=%s %s" % (resp.get("len"), "first" if before is None else
                                            "changed" if now != before else "same"))
                if now:
                    last[(side, pointer)] = now
            expected, actual = marks
        compared += 1
        if expected != actual:
            diffs.append("%d %s: recorded %s, model %s" % (t, text, expected, actual))
    return diffs, compared


def register_range(text):
    """`F7` or `F7-FC`, as an inclusive (low, high) pair."""
    lo, _, hi = text.partition("-")
    return int(lo, 16), int(hi or lo, 16)


def build(model, sources, out):
    header, cls = model.split(":")
    cmd = ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
           "-I%s" % (ROOT / "src"), "-I%s" % (ROOT / "devices" / "src"),
           "-DEB_MODEL_HEADER=\"%s\"" % header, "-DEB_MODEL_CLASS=%s" % cls,
           str(MAIN), *[str(s) for s in sources], "-o", str(out)]
    subprocess.run(cmd, check=True)


def replay(binary, lines, addr, channels):
    args = [str(binary), "--addr", "%02X" % addr]
    for channel, data in channels:
        args += ["--channel", str(channel), data]
    result = subprocess.run(args, input="\n".join(lines) + "\n", capture_output=True,
                            text=True, check=True)
    return result.stdout.splitlines()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("trace", type=Path)
    ap.add_argument("--addr", required=True, help="the model's address, hex")
    ap.add_argument("--model", required=True, help="header.h:ClassName")
    ap.add_argument("--source", action="append", default=[], type=Path)
    ap.add_argument("--channel", nargs=2, action="append", default=[],
                    metavar=("N", "HEX"), help="preload a world channel")
    ap.add_argument("--mask", action="append", default=[],
                    help="register whose read values are not compared (hex)")
    ap.add_argument("--changes", action="append", default=[],
                    help="register or range (F7-FC) whose reads are compared by whether "
                         "the value changed since the previous read from the same "
                         "pointer, not by the value (hex)")
    ap.add_argument("--only-addr", action="store_true")
    ap.add_argument("--out", type=Path, help="write the model's trace here")
    args = ap.parse_args()

    addr = int(args.addr, 16)
    recorded = args.trace.read_text().splitlines()
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp) / "replay"
        build(args.model, args.source, binary)
        modelled = replay(binary, ask_full_length(recorded), addr, args.channel)
    if args.out:
        args.out.write_text("\n".join(modelled) + "\n")
    for line in modelled:
        if " mark src=model " in line or line.startswith("# dump"):
            print(line)
    result = compare(recorded, modelled, addr, {m.upper() for m in args.mask}, args.only_addr,
                     [register_range(c) for c in args.changes])
    if isinstance(result, list):
        print(result[0])
        sys.exit(1)
    diffs, compared = result
    for d in diffs:
        print(d)
    print("%d transfers compared, %d differ" % (compared, len(diffs)))
    sys.exit(1 if diffs else 0)


if __name__ == "__main__":
    main()
