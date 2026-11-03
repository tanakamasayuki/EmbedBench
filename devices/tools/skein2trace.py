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
           capture's host time (captures[].host_ns, the host's monotonic
           clock in ns, as log[].host_ns), and says so with clock=host.
           Only wireskein-run/1 is read.

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

--knowledge FILE@ADDR (EmbedBenchDeviceData knowledge/<chip>.yaml and the
part's I2C address, 0x70) folds what the file's decode_artefacts name: a transfer
that the decode shows right after a given write to the part, and that is
that write's own doing rather than a transfer of its own (QMP6988's reset
byte E6, read by the decoder as an address: 0x73 written). It becomes
`mark folded=<key> addr=<addr>`. The decode is right to show it; knowing
it is not a transfer is knowledge of the part. For a run, --provenance
provenance.yaml --knowledge-dir knowledge/ finds the files and addresses
from the run's targets; --knowledge is for input with no provenance.

Marker values are reduced to [A-Za-z0-9_-] (anything else becomes `_`),
and the application's text is percent-encoded, so no marker can read as
an event. Checked against a P4X capture of a real bus (X66).

Usage:
    skein2trace.py i2c.json --capture c0001.wireskein [--run run.json]
        [--provenance provenance.yaml --knowledge-dir knowledge/]
        [--knowledge knowledge/qmp6988.yaml@0x70]
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


def load_folds(specs):
    """--knowledge FILE@ADDR: every decode artefact a part's knowledge file
    names (its decode_artefacts, by key), for the part at I2C address ADDR
    (0x70)."""
    import yaml
    rules = []
    for spec in specs:
        path, _, addr = spec.rpartition("@")
        if not path or not addr.lower().startswith("0x"):
            raise SystemExit("--knowledge %s: expected FILE@0x<i2c address>" % spec)
        data = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
        for item in data.get("decode_artefacts") or []:
            then = item["then"]
            rules.append((int(addr, 16), bytes.fromhex(item["write"]),
                          int(str(then["addr"]), 16), then["rw"],
                          bytes.fromhex(then.get("bytes") or ""),
                          slug(item["key"])))
    return rules


def folds_from_provenance(provenance, knowledge_dir):
    """The run's provenance.yaml names each part as {chip, addr}: fold what
    knowledge/<chip>.yaml says, at that address."""
    import yaml
    data = yaml.safe_load(Path(provenance).read_text(encoding="utf-8"))
    specs = []
    for target in data.get("targets") or []:
        path = Path(knowledge_dir) / ("%s.yaml" % target["chip"])
        if path.exists():
            specs.append("%s@%s" % (path, target["addr"]))
    return load_folds(specs)


def folded(transactions, rules):
    """Indexes of transfers that are a known artefact of the transfer just
    before them, by key."""
    out = {}
    for i in range(1, len(transactions)):
        x, before = transactions[i], transactions[i - 1]
        for part, write, addr, rw, data, key in rules:
            if (before["addr"] == part and before["rw"] == "write" and
                    bytes.fromhex(before.get("bytes") or "") == write and
                    x["addr"] == addr and x["rw"] == rw and
                    bytes.fromhex(x.get("bytes") or "") == data):
                out[i] = key
    return out


def transfer_lines(transactions, hz, start_ns, tick0, rules=()):
    out = []
    last = None
    folds = folded(transactions, rules)
    for index, x in enumerate(transactions):
        time = tick_us(x["s"], hz, start_ns, tick0)
        if index in folds:
            out.append((time, "mark folded=%s addr=%02X" % (folds[index], x["addr"])))
            continue
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


RUN_FORMAT = "wireskein-run/1"


def mark_lines(run, capture_file, start_ns):
    if run.get("format") != RUN_FORMAT:
        raise SystemExit("run.json: format %r, this reads %s only" % (
            run.get("format"), RUN_FORMAT))
    entry = next((c for c in run.get("captures", [])
                  if c.get("file") == capture_file), None)
    armed_ns = entry.get("host_ns") if entry else None
    out = []
    case = phase = None
    for line in run.get("log", []):
        if "probe_ns" in line:
            time, clock = int(line["probe_ns"]) // 1000, ""
        elif armed_ns is not None:
            # The host's markers have only host time; the capture's arming
            # is the one point both clocks share.
            time = (start_ns + line["host_ns"] - armed_ns) // 1000
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


def convert(i2c, capture_path, run=None, rules=()):
    hz, start_ns, tick0 = load_capture(capture_path)
    lines = transfer_lines(i2c.get("transactions", []), hz, start_ns, tick0,
                           rules)
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
    ap.add_argument("--knowledge", action="append", default=[],
                    metavar="FILE@ADDR",
                    help="a part's knowledge file and the part's I2C address (0x70): "
                    "fold the decode artefacts it names")
    ap.add_argument("--provenance", type=Path,
                    help="the run's provenance.yaml: fold what the knowledge "
                    "of each of its targets says, at the target's address")
    ap.add_argument("--knowledge-dir", type=Path,
                    help="EmbedBenchDeviceData knowledge/, for --provenance")
    args = ap.parse_args()
    run = json.loads(args.run.read_text()) if args.run else None
    rules = load_folds(args.knowledge)
    if args.provenance:
        if not args.knowledge_dir:
            raise SystemExit("--provenance needs --knowledge-dir")
        rules += folds_from_provenance(args.provenance, args.knowledge_dir)
    for line in convert(json.loads(args.i2c.read_text()), args.capture, run,
                        rules):
        print(line)


if __name__ == "__main__":
    main()
