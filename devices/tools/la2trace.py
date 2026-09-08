#!/usr/bin/env python3
"""Turn a logic analyzer's I2C decode into EmbedBench event lines.

A bus-level capture (START, address, data bytes, ACK/NACK, STOP) is the
one source that needs no change to the application at all. This tool
folds it into the transfer-level lines trace2regtable.py and trace2tape.py
read, with the master's view of each transfer:

    <time_us> i2c.req addr=76 data=F425 stop=1 [rs]
    <time_us> i2c.resp status=0
    <time_us> i2c.rd.req addr=76 req=1 stop=1 [rs]
    <time_us> i2c.rd.resp len=1 data=08

status follows Wire::endTransmission(): 0 acknowledged, 2 address NACK,
3 data NACK. `stop=0` is a transfer followed by a repeated START; `rs`
marks the transfer that continued it to the same address. A read whose
address was not acknowledged answers len=0.

Input formats, told apart by the CSV header:

  generic   time_us,event,value
            event: start | address | data | ack | nack | stop
            value: for address, hex plus R or W ("76W", "0x76 R");
                   for data, one hex byte
            An ack/nack row qualifies the address or data row before it.

  saleae    name,type,start_time,duration,ack,address,data,read
            The column layout of Logic 2's I2C analyzer export, as
            documented; start_time in seconds. Written from the column
            names, NOT yet checked against a real export — if yours
            differs, the generic format is a few lines of spreadsheet away.

Usage:
    la2trace.py capture.csv > capture.trace
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

ADDR_RE = re.compile(r"^(?:0x)?([0-9A-Fa-f]{1,2})\s*([RWrw])$")


class Transfer:
    def __init__(self, time, address, read):
        self.time = time
        self.address = address
        self.read = read
        self.address_acked = None
        self.data = bytearray()
        self.acks = []
        self.stop = 1


def parse_generic(rows):
    events = []
    for row in rows:
        if not row or not row.get("event"):
            continue
        time = int(float(row["time_us"]))
        event = row["event"].strip().lower()
        value = (row.get("value") or "").strip()
        events.append((time, event, value))
    return events


def parse_saleae(rows):
    events = []
    for row in rows:
        kind = (row.get("type") or "").strip().lower()
        if not kind:
            continue
        time = int(round(float(row["start_time"]) * 1_000_000))
        ack = (row.get("ack") or "").strip().lower() in ("true", "1", "ack")
        if kind == "start":
            events.append((time, "start", ""))
        elif kind == "address":
            addr = int(str(row.get("address", "0")).strip(), 16)
            read = (row.get("read") or "").strip().lower() in ("true", "1")
            events.append((time, "address", "%02X%s" % (addr, "R" if read else "W")))
            events.append((time, "ack" if ack else "nack", ""))
        elif kind == "data":
            events.append((time, "data", "%02X" % int(str(row.get("data", "0")).strip(), 16)))
            events.append((time, "ack" if ack else "nack", ""))
        elif kind == "stop":
            events.append((time, "stop", ""))
    return events


def to_lines(events):
    lines = []
    open_xfer = None
    last = None  # the previous transfer, for repeated-start detection

    def close(x, stop):
        nonlocal last
        x.stop = stop
        rs = (last is not None and last.stop == 0 and last.address == x.address)
        suffix = " rs" if rs else ""
        if x.read:
            got = bytes(x.data) if x.address_acked else b""
            lines.append("%d i2c.rd.req addr=%02X req=%u stop=%u%s" % (
                x.time, x.address, len(x.data), stop, suffix))
            lines.append("%d i2c.rd.resp len=%u data=%s" % (
                x.time, len(got), got.hex().upper()))
        else:
            if x.address_acked is False:
                status = 2
            elif False in x.acks:
                status = 3
            else:
                status = 0
            lines.append("%d i2c.req addr=%02X data=%s stop=%u%s" % (
                x.time, x.address, bytes(x.data).hex().upper(), stop, suffix))
            lines.append("%d i2c.resp status=%u" % (x.time, status))
        last = x

    expecting_address = False
    for time, event, value in events:
        if event == "start":
            if open_xfer is not None:
                close(open_xfer, 0)
                open_xfer = None
            expecting_address = True
        elif event == "address":
            m = ADDR_RE.match(value)
            if not m:
                raise SystemExit("bad address value %r at %d us" % (value, time))
            open_xfer = Transfer(time, int(m.group(1), 16),
                                 m.group(2).upper() == "R")
            expecting_address = False
        elif event == "data":
            if open_xfer is None:
                continue
            open_xfer.data.append(int(value, 16))
        elif event in ("ack", "nack"):
            if open_xfer is None:
                continue
            acked = event == "ack"
            if open_xfer.address_acked is None:
                open_xfer.address_acked = acked
            elif not open_xfer.read:
                open_xfer.acks.append(acked)
            # a master's NACK on the last read byte is the protocol, not an error
        elif event == "stop":
            if open_xfer is not None:
                close(open_xfer, 1)
                open_xfer = None
    if open_xfer is not None:
        close(open_xfer, 1)
    return lines


def convert(text):
    rows = list(csv.DictReader(text.splitlines()))
    if not rows:
        return []
    header = {k.strip().lower() for k in rows[0].keys() if k}
    if {"time_us", "event"} <= header:
        events = parse_generic(rows)
    elif {"type", "start_time"} <= header:
        events = parse_saleae(rows)
    else:
        raise SystemExit("unknown CSV header: %s" % ", ".join(sorted(header)))
    return to_lines(events)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("csv", type=Path)
    ap.add_argument("--out", type=Path, help="write here instead of stdout")
    args = ap.parse_args(argv)
    lines = convert(args.csv.read_text())
    text = "\n".join(lines) + ("\n" if lines else "")
    if args.out:
        args.out.write_text(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
