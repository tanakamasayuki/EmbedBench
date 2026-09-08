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
            Any analyzer's table export reshapes into this: one row per
            bus event, its time in microseconds, and for address rows the
            direction. A Saleae Logic 2 I2C export, for instance, already
            has type / start_time / address / data / ack / read columns;
            a mapping for it is not built in because no real export has
            been checked against (X65) — the sigrok text below is what
            such a mapping looks like once it has been.

  sigrok    the text sigrok-cli prints for the i2c decoder, one annotation
            per line, optionally with sample numbers in front:
                12345-12400 i2c-1: Start
                12401-12500 i2c-1: Address write: A0
                12501-12510 i2c-1: ACK
                12511-12600 i2c-1: Data write: 01
                ...         i2c-1: Stop
            Run it with every class the folding needs:
              -A i2c=start:repeat-start:stop:ack:nack:address-read:\
                     address-write:data-read:data-write
            and --protocol-decoder-samplenum plus --samplerate HERE for
            timestamps (without them the lines carry no time). The
            decoder's default address_format=shifted prints the 7-bit
            address (a BH1750 shows as 23); if it was run with
            address_format=unshifted it prints the address byte with the
            R/W bit (46 for a write, 47 for a read) — pass
            --address-format unshifted then. Checked against two real
            captures from sigrok-dumps (BH1750 at 500 kHz, SHT31 at 8 MHz)
            decoded by sigrok-cli 0.7.2 (X65).

Usage:
    la2trace.py capture.csv > capture.trace
    la2trace.py decode.txt --samplerate 1000000 > capture.trace
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

ADDR_RE = re.compile(r"^(?:0x)?([0-9A-Fa-f]{1,2})\s*([RWrw])$")
SIGROK_RE = re.compile(r"^\s*(?:(\d+)-(\d+)\s+)?i2c(?:-\d+)?:\s*(.+?)\s*$")


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


def parse_sigrok(text, samplerate=None, address_format="shifted"):
    events = []
    for raw in text.splitlines():
        m = SIGROK_RE.match(raw)
        if not m:
            continue
        time = None
        if m.group(1) is not None and samplerate:
            time = int(round(int(m.group(1)) * 1_000_000 / samplerate))
        text_ = m.group(3)
        low = text_.lower()
        if low in ("start", "start repeat"):
            events.append((time, "start", ""))
        elif low == "stop":
            events.append((time, "stop", ""))
        elif low in ("ack", "nack"):
            events.append((time, low, ""))
        elif low.startswith("address write:") or low.startswith("address read:"):
            value = int(text_.split(":", 1)[1].strip(), 16)
            if address_format == "unshifted":
                value >>= 1  # the raw byte carries the R/W bit
            read = low.startswith("address read")
            events.append((time, "address", "%02X%s" % (value, "R" if read else "W")))
        elif low.startswith("data write:") or low.startswith("data read:"):
            events.append((time, "data", text_.split(":", 1)[1].strip()))
        # "Write", "Read", bits and warnings carry nothing the folding needs
    return events


def stamp(time, text):
    return text if time is None else "%d %s" % (time, text)


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
            lines.append(stamp(x.time, "i2c.rd.req addr=%02X req=%u stop=%u%s" % (
                x.address, len(x.data), stop, suffix)))
            lines.append(stamp(x.time, "i2c.rd.resp len=%u data=%s" % (
                len(got), got.hex().upper())))
        else:
            if x.address_acked is False:
                status = 2
            elif False in x.acks:
                status = 3
            else:
                status = 0
            lines.append(stamp(x.time, "i2c.req addr=%02X data=%s stop=%u%s" % (
                x.address, bytes(x.data).hex().upper(), stop, suffix)))
            lines.append(stamp(x.time, "i2c.resp status=%u" % status))
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
                raise SystemExit("bad address value %r at %s us" % (value, time))
            if open_xfer is not None:
                # No Start annotation in this export: a new address begins a
                # new transfer, and the old one is taken as having stopped.
                close(open_xfer, 1)
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


def convert(text, samplerate=None, address_format="shifted"):
    first = next((l for l in text.splitlines() if l.strip()), "")
    if SIGROK_RE.match(first):
        return to_lines(parse_sigrok(text, samplerate, address_format))
    rows = list(csv.DictReader(text.splitlines()))
    if not rows:
        return []
    header = {k.strip().lower() for k in rows[0].keys() if k}
    if {"time_us", "event"} <= header:
        return to_lines(parse_generic(rows))
    raise SystemExit("unknown input: not sigrok-cli text, and the CSV header "
                     "is not time_us,event,value but %s — reshape the export "
                     "into that (see the module docstring)"
                     % ", ".join(sorted(header)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("csv", type=Path, help="CSV export or sigrok-cli text")
    ap.add_argument("--out", type=Path, help="write here instead of stdout")
    ap.add_argument("--samplerate", type=float,
                    help="sigrok input: samples per second, to turn sample "
                    "numbers into microseconds")
    ap.add_argument("--address-format", choices=("shifted", "unshifted"),
                    default="shifted", help="sigrok input: the decoder's "
                    "address_format option (default shifted, the 7-bit address)")
    args = ap.parse_args(argv)
    lines = convert(args.csv.read_text(), args.samplerate, args.address_format)
    text = "\n".join(lines) + ("\n" if lines else "")
    if args.out:
        args.out.write_text(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
