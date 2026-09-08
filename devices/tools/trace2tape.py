#!/usr/bin/env python3
"""Turn a captured trace into a TapeModel tape.

Where trace2regtable.py distils a trace into what a part IS (a register
table), this keeps what the part DID: every transfer and serial exchange in
order, as a C++ table `tape_model` plays back step by step. Run the
application against the tape first — the model says, through the
diagnostic path, exactly where the application leaves the recording (X64).

Input: the same event lines trace2regtable.py reads (see there), plus
    uart.tx <text>          bytes the application sent   -> a kSerialIn step
    dev.tx <text>           bytes the device answered    -> a kSerialOut step,
                            `delayUs` after the previous step
    spi.xfer mosi=.. miso=..  one transfer call (CaptureSPI) -> a kSpi step
    spi.req mosi=XX / spi.resp miso=XX   one byte (the environments); a run
                            of them with nothing else between is one kSpi step
A trace with several devices on it is split by source: --addr keeps one
I2C address, --serial the serial exchange, --spi the SPI transfers; each
becomes its own tape for its own TapeModel.
Text is as the environments print it: printable characters, \\r \\n \\t
escapes, `data=HEX` for short binary, `empty`. A `len=N crc=XX` label is a
checksum of bytes the log did not keep, so that step is left out with a
comment; a read whose payload the log truncated is emitted with the bytes
it kept and marked INCOMPLETE. A capture meant for a tape should carry
every byte (CaptureWire does).

Usage:
    trace2tape.py TRACE --name Env --out env_tape.h [--addr 76 | --serial | --spi]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import trace2regtable as t2r  # noqa: E402

PORT_RE = re.compile(r"^port=\d+\s+")
DATA_RE = re.compile(r"^data=([0-9A-Fa-f]*)$")
CRC_RE = re.compile(r"^len=(\d+) crc=[0-9A-Fa-f]+$")


def serial_payload(rest):
    """Bytes for a uart.tx / dev.tx label, or None when the log kept only
    a checksum."""
    text = PORT_RE.sub("", rest.strip())
    if text == "empty":
        return b""
    m = DATA_RE.match(text)
    if m:
        return bytes.fromhex(m.group(1))
    if CRC_RE.match(text):
        return None
    out = bytearray()
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text) and text[i + 1] in "rnt":
            out.append({"r": 13, "n": 10, "t": 9}[text[i + 1]])
            i += 2
        else:
            out.extend(c.encode("latin-1"))
            i += 1
    return bytes(out)


class Step:
    def __init__(self, kind, time, source, data=b"", status=0, stop=1,
                 request=0, delay=0, note=None, length=None):
        self.kind = kind
        self.time = time
        self.source = source
        self.data = data
        self.status = status
        self.stop = stop
        self.request = request
        self.delay = delay
        self.note = note
        # kSpi: `data` is MOSI then MISO, `length` bytes each.
        self.length = len(data) if length is None else length


def build(events, address, include=("i2c", "serial", "spi")):
    xfers = t2r.pair_transfers(events)
    by_index = {x.req.index: x for x in xfers
                if "i2c" in include and (address is None or x.addr == address)}
    steps = []
    skipped = []
    prev_time = 0
    spi_pending = None   # a run of single-byte transfers being merged
    spi_req = None       # the spi.req waiting for its spi.resp

    def flush_spi():
        nonlocal spi_pending, prev_time
        if spi_pending is not None:
            mosi, miso, time, source = spi_pending
            steps.append(Step("kSpi", time, source, data=mosi + miso,
                              length=len(mosi)))
            prev_time = time if time is not None else prev_time
            spi_pending = None

    for e in events:
        if e.kind in ("spi.req", "spi.resp", "spi.xfer", "spi.bulk"):
            if "spi" not in include:
                continue
            if e.kind == "spi.req":
                spi_req = e
            elif e.kind == "spi.resp" and spi_req is not None:
                mosi = t2r.hexbytes(spi_req.fields.get("mosi"))
                miso = t2r.hexbytes(e.fields.get("miso"))
                if spi_pending is None:
                    spi_pending = (b"", b"", spi_req.time,
                                   "spi bytes from " + spi_req.rest)
                spi_pending = (spi_pending[0] + mosi, spi_pending[1] + miso,
                               spi_pending[2], spi_pending[3])
                spi_req = None
            elif e.kind == "spi.xfer":
                flush_spi()
                mosi = t2r.hexbytes(e.fields.get("mosi"))
                miso = t2r.hexbytes(e.fields.get("miso"))
                if len(mosi) != len(miso) or not mosi:
                    skipped.append((e.time, e.kind, e.rest))
                    continue
                steps.append(Step("kSpi", e.time, e.rest, data=mosi + miso,
                                  length=len(mosi)))
                prev_time = e.time if e.time is not None else prev_time
            else:  # spi.bulk: a checksum, not the bytes
                flush_spi()
                skipped.append((e.time, e.kind, e.rest))
            continue
        if e.kind.startswith("diag."):
            continue  # commentary does not break a run of SPI bytes
        flush_spi()
        if e.index in by_index:
            x = by_index[e.index]
            if x.kind == "W":
                steps.append(Step("kWrite", e.time, e.rest, data=x.data,
                                  status=x.status, stop=1 if x.stop else 0))
            else:
                note = None
                if x.truncated:
                    note = ("INCOMPLETE: %d bytes answered, %d kept by the log"
                            % (x.got_len, len(x.got)))
                steps.append(Step("kRead", e.time, e.rest, data=x.got,
                                  stop=1 if x.stop else 0, request=x.want,
                                  note=note))
        elif e.kind == "uart.tx" and "serial" in include:
            payload = serial_payload(e.rest)
            if payload is None:
                skipped.append((e.time, e.kind, e.rest))
                continue
            steps.append(Step("kSerialIn", e.time, e.rest, data=payload))
        elif e.kind == "dev.tx" and "serial" in include:
            payload = serial_payload(e.rest)
            if payload is None:
                skipped.append((e.time, e.kind, e.rest))
                continue
            delay = max(0, (e.time or 0) - prev_time)
            steps.append(Step("kSerialOut", e.time, e.rest, data=payload,
                              delay=delay))
        else:
            continue
        prev_time = e.time if e.time is not None else prev_time
    flush_spi()
    return steps, skipped


def render(steps, skipped, name, source, address):
    ident = t2r.cpp_ident(name)
    out = []
    out.append("// Generated by devices/tools/trace2tape.py from %s%s: %d steps."
               % (source, "" if address is None else
                  " (device address 0x%02X)" % address, len(steps)))
    out.append("// The recorded session, played back in order by TapeModel.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <tape_model.h>")
    out.append("")
    for i, s in enumerate(steps):
        out.append("// step %d @%s us: %s%s" % (
            i, s.time, s.source, ("  // " + s.note) if s.note else ""))
        if s.data:
            out.append("static const uint8_t k%sTape_%d[%d] = {%s};" % (
                ident, i, len(s.data), ", ".join("0x%02X" % b for b in s.data)))
    for time, kind, rest in skipped:
        out.append("// left out @%s us: %s %s (the log kept a checksum, not "
                   "the bytes)" % (time, kind, rest))
    out.append("")
    out.append("static const TapeStep k%sTapeSteps[%d] = {" % (ident, len(steps)))
    for i, s in enumerate(steps):
        data = ("k%sTape_%d" % (ident, i)) if s.data else "nullptr"
        out.append("    {TapeModel::%s, %d, %d, %d, %d, %s, %d}," % (
            s.kind, s.status, s.stop, s.length, s.request, data, s.delay))
    out.append("};")
    out.append("static const TapeSpec k%sTapeSpec = {k%sTapeSteps, %d};" % (
        ident, ident, len(steps)))
    out.append("")
    out.append("class %sTape : public TapeModel {" % ident)
    out.append(" public:")
    out.append("  %sTape() : TapeModel(k%sTapeSpec) {}" % (ident, ident))
    out.append("};")
    out.append("")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("trace", type=Path)
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--addr", help="keep only the I2C transfers to this "
                    "address (hex); needed when the trace shows several")
    ap.add_argument("--serial", action="store_true",
                    help="keep only the serial exchange")
    ap.add_argument("--spi", action="store_true",
                    help="keep only the SPI transfers")
    args = ap.parse_args(argv)
    events = t2r.parse(args.trace.read_text().splitlines())
    addresses = sorted({x.addr for x in t2r.pair_transfers(events)
                        if x.addr is not None})
    address = None
    if args.addr is not None:
        include = ("i2c",)
        address = int(args.addr, 16)
    elif args.serial:
        include = ("serial",)
    elif args.spi:
        include = ("spi",)
    else:
        include = ("i2c", "serial", "spi")
        if len(addresses) > 1:
            sys.exit("trace shows %d device addresses (%s); pass --addr, "
                     "--serial or --spi" % (
                         len(addresses), ", ".join("%02X" % x for x in addresses)))
        address = addresses[0] if addresses else None
    steps, skipped = build(events, address, include)
    args.out.write_text(render(steps, skipped, args.name, args.trace.name,
                               address))
    kinds = {}
    for s in steps:
        kinds[s.kind] = kinds.get(s.kind, 0) + 1
    print("%s: %d steps (%s), %d left out" % (
        args.name, len(steps),
        ", ".join("%d %s" % (n, k[1:].lower()) for k, n in sorted(kinds.items())) or "none",
        len(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
