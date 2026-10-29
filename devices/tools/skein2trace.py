#!/usr/bin/env python3
"""Turn a wireskein I2C decode and its run into EmbedBench event lines.

An OEP probe runs a library case against a real part, captures the same
lines itself, and logs case markers on the clock the capture uses.
wireskein decodes the capture; this tool writes the decode as the
transfer-level lines trace2regtable.py and trace2tape.py read, with the
run's markers between them, all on one time axis: microseconds of the
probe's clock.

    <time_us> mark case=test_wire
    <time_us> mark case=test_wire phase=write_2_bytes_to_0x50
    <time_us> i2c.req addr=50 data= stop=1
    <time_us> i2c.resp status=2
    <time_us> mark src=dut text=endTransmission%3D4
    <time_us> mark case=test_wire phase=write_2_bytes_to_0x50 end=1

Inputs
------
I2C_JSON   wireskein's transactions, as its analysis export writes them:
           {"transactions": [{"s", "e", "addr", "rw", "addr_ack",
           "bytes" (hex), "acks", "start_cond" (S | Sr),
           "end_cond" (P | Sr | null), "scl_low_ticks", "scl_waits"}]}
           s / e and scl_waits[].ticks are capture ticks.
--capture  the .wireskein file: capture.json gives tick_hz and the probe
           time of tick 0 (parts[0].meta.start_ns).
--run      run.json (optional): its log lines become `mark` lines. "# x"
           opens case x and a bare "#" closes it; "## y" and "##" do the
           same for a phase. A line with src=dut is the application's own
           output. A line that carries probe_ns is placed by it; one that
           does not (the host's own markers) is placed through the
           capture's host time t0, and says so with clock=host.

Lines follow la2trace.py: both lines of a transfer carry its START time;
status is 0 / 2 address NACK / 3 data NACK, read off the bus; a read
whose address went unanswered carries status=2 as well. That is the
bus's view, not the library's: the first sample's Wire returned 4 (no
pull-ups) for a transfer the bus shows as an address NACK. `rs` marks a
transfer that a repeated start continued to the same address; stop=0 is
a transfer that ended in one. A transfer still open when the window
closed (end_cond null) is written with stop=1 and reported on stderr.
`scl_wait=<us>` on a device line totals the transfer's long SCL lows
(wireskein's scl_waits); the line level cannot tell who held SCL low, so
it is a wait, not a stretch.

Marker values are reduced to [A-Za-z0-9_-] (anything else becomes `_`),
and the application's text is percent-encoded, so no marker can read as
an event. Checked against a P4X capture of a real bus (X66).

Usage:
    skein2trace.py i2c.json --capture c0001.wireskein [--run run.json]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from fractions import Fraction
from pathlib import Path
from urllib.parse import quote

SLUG_RE = re.compile(r"[^A-Za-z0-9_-]")


def slug(text):
    return SLUG_RE.sub("_", text.strip()) or "_"


def load_capture(path):
    with zipfile.ZipFile(path) as z:
        cap = json.loads(z.read("capture.json"))
    num, den = cap["tick_hz"]
    parts = cap.get("parts") or [{}]
    if len(parts) > 1:
        print("warning: %s has %d parts; times use part 0's start" % (
            path, len(parts)), file=sys.stderr)
    meta = parts[0].get("meta", {})
    if "start_ns" not in meta:
        raise SystemExit("%s: no start_ns, so no probe time to put "
                         "transfers on" % path)
    return Fraction(num, den), int(meta["start_ns"]), int(parts[0].get("tick0", 0))


def tick_us(tick, hz, start_ns, tick0):
    return int(round(Fraction(start_ns, 1000) +
                     (Fraction(tick) - tick0) * 1_000_000 / hz))


def transfer_lines(transactions, hz, start_ns, tick0):
    out = []
    last = None
    for x in transactions:
        time = tick_us(x["s"], hz, start_ns, tick0)
        if x.get("end_cond") is None:
            print("warning: transfer at %d us still open at the window's "
                  "end" % time, file=sys.stderr)
        stop = 0 if x.get("end_cond") == "Sr" else 1
        rs = (x.get("start_cond") == "Sr" and last is not None and
              last["addr"] == x["addr"])
        suffix = " rs" if rs else ""
        data = bytes.fromhex(x.get("bytes") or "")
        waits = sum(w["ticks"] for w in x.get("scl_waits") or [])
        wait = (" scl_wait=%d" % round(Fraction(waits) * 1_000_000 / hz)
                if waits else "")
        if x["rw"] == "read":
            got = data if x["addr_ack"] else b""
            out.append((time, "i2c.rd.req addr=%02X req=%u stop=%u%s" % (
                x["addr"], len(data), stop, suffix)))
            out.append((time, "i2c.rd.resp len=%u data=%s%s%s" % (
                len(got), got.hex().upper(),
                "" if x["addr_ack"] else " status=2", wait)))
        else:
            if not x["addr_ack"]:
                status = 2
            elif False in (x.get("acks") or []):
                status = 3
            else:
                status = 0
            out.append((time, "i2c.req addr=%02X data=%s stop=%u%s" % (
                x["addr"], data.hex().upper(), stop, suffix)))
            out.append((time, "i2c.resp status=%u%s" % (status, wait)))
        last = x
    return out


def mark_lines(run, capture_file, start_ns):
    entry = next((c for c in run.get("captures", [])
                  if c.get("file") == capture_file), None)
    t0 = entry.get("t0") if entry else None
    out = []
    case = phase = None
    for line in run.get("log", []):
        if "probe_ns" in line:
            time, clock = int(line["probe_ns"]) // 1000, ""
        elif t0 is not None:
            # The host's markers have only host time; the capture's t0 is
            # the one point both clocks share.
            time = int(round(start_ns / 1000 + (line["t"] - t0) * 1e6))
            clock = " clock=host"
        else:
            continue
        text = line.get("text", "")
        if line.get("src") == "dut":
            out.append((time, "mark src=dut text=%s%s" % (
                quote(text, safe=""), clock)))
            continue
        level = len(text) - len(text.lstrip("#"))
        name = text[level:].strip()
        if level == 1:
            if name:
                case, phase = slug(name), None
                out.append((time, "mark case=%s%s" % (case, clock)))
            elif case is not None:
                out.append((time, "mark case=%s end=1%s" % (case, clock)))
                case = phase = None
        elif level == 2:
            where = "case=%s " % case if case else ""
            if name:
                phase = slug(name)
                out.append((time, "mark %sphase=%s%s" % (where, phase, clock)))
            elif phase is not None:
                out.append((time, "mark %sphase=%s end=1%s" % (
                    where, phase, clock)))
                phase = None
    return out


def convert(i2c, capture_path, run=None):
    hz, start_ns, tick0 = load_capture(capture_path)
    lines = transfer_lines(i2c.get("transactions", []), hz, start_ns, tick0)
    if run is not None:
        lines += mark_lines(run, Path(capture_path).name, start_ns)
    # Stable: a transfer and a marker at the same microsecond keep the
    # order they were produced in, transfers first.
    lines.sort(key=lambda item: item[0])
    return ["%d %s" % (time, text) for time, text in lines]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("i2c", type=Path, help="wireskein I2C transactions JSON")
    ap.add_argument("--capture", type=Path, required=True,
                    help="the .wireskein capture the transactions came from")
    ap.add_argument("--run", type=Path, help="run.json, for the markers")
    args = ap.parse_args()
    run = json.loads(args.run.read_text()) if args.run else None
    for line in convert(json.loads(args.i2c.read_text()), args.capture, run):
        print(line)


if __name__ == "__main__":
    main()
