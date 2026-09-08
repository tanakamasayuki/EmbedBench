#!/usr/bin/env python3
"""Build a RegTableModel scaffold from a captured I2C trace.

The reproduction side of EmbedBench reads a trace to check an application.
This tool reads the same trace the other way round, to start writing the
DEVICE: it turns what one part answered into the table `regtable_model`
runs from, and lists everything the capture could not settle so a person
knows exactly which hooks to write (X63).

Input
-----
The one-line-per-event trace both environments print. Only these events
matter; every other line is ignored:

    i2c.req addr=76 data=F425 stop=1 [rs]       i2c.resp status=0 [re=N]
    i2c.rd.req addr=76 req=1 stop=1 [rs]        i2c.rd.resp len=1 data=08 [re=N]
    chan.write chan=0 data=7FE000               gpio.inject line=0 val=1
    gpio.inject pin=27 0->1 match=0             dev.note <text>
    diag.<name> ...

The columns in front (sequence, time, context, origin) are optional, so a
capture shim on a real board can print the same event lines with just a
microsecond timestamp first:

    123456 i2c.req addr=76 data=F425 stop=1
    123500 i2c.resp status=0

A folded repeat (`x8..007000`, see X53) counts as one observation.

What it infers
--------------
    register         every pointer value that returned data or took a write
    width            the longest read or write seen through that pointer
    reset contents   the first read, when no write to it came before
    writable         a multi-byte write acknowledged with status 0
    repeated start   required iff every data-returning read continued a
                     pointer write
    channel map      a chan.write payload that later reads back verbatim
                     from exactly one register

What it cannot infer is listed as TODO items in the header and report:
a register that changed on its own (with what happened in between), a
line that moved, a read shorter than requested, a payload the log cut
short, a channel payload that appears nowhere verbatim. Each is a hook
for a class derived from the generated one.

Usage
-----
    trace2regtable.py TRACE --name Env --out env_table.h [--report env.txt]
                      [--addr 76]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

KIND_RE = re.compile(
    r"(?<![\w.])(i2c\.rd\.req|i2c\.rd\.resp|i2c\.req|i2c\.resp|chan\.write|"
    r"gpio\.inject|dev\.note|dev\.tx|uart\.tx|uart\.rx|diag\.[\w.]+|dump)"
    r"(?=\s|$)")
FOLD_RE = re.compile(r"\s+x(\d+)\.\.(\d+)$")
KV_RE = re.compile(r"(\w+)=(\S*)")
EDGE_RE = re.compile(r"^(\d)->(\d)$")


class Event:
    def __init__(self, index, seq, time, ctx, origin, kind, rest, fields,
                 flags, fold, link):
        self.index = index
        self.seq = seq
        self.time = time
        self.ctx = ctx
        self.origin = origin
        self.kind = kind
        self.rest = rest
        self.fields = fields
        self.flags = flags
        self.fold = fold
        self.link = link

    def addr(self):
        raw = self.fields.get("addr")
        if raw is None:
            return None
        m = re.match(r"[0-9A-Fa-f]+", raw)
        return int(m.group(0), 16) if m else None


def parse_line(text, index):
    text = text.rstrip()
    m = KIND_RE.search(text)
    if not m:
        return None
    head = text[:m.start()].split()
    rest = text[m.end():].strip()
    fold = None
    fm = FOLD_RE.search(rest)
    if fm:
        fold = (int(fm.group(1)), int(fm.group(2)))
        rest = rest[:fm.start()]
    digits = [t for t in head if t.isdigit()]
    seq = time = None
    if len(digits) >= 2:
        seq, time = int(digits[-2]), int(digits[-1])
    elif len(digits) == 1:
        time = int(digits[0])
    ctx = next((t for t in head if t in ("main", "tick", "isr")), None)
    origin = next((t for t in head if t in ("app", "dev", "dir", "core",
                                             "diag")), None)
    fields = {}
    flags = []
    for tok in rest.split():
        kv = KV_RE.fullmatch(tok)
        if kv:
            fields[kv.group(1)] = kv.group(2)
        else:
            flags.append(tok)
    link = None
    if fields.get("re", "").isdigit():
        link = int(fields["re"])
    return Event(index, seq, time, ctx, origin, m.group(1), rest, fields,
                 flags, fold, link)


def parse(lines):
    events = []
    for i, line in enumerate(lines):
        e = parse_line(line, i)
        if e is not None:
            events.append(e)
    return events


def hexbytes(text):
    text = text or ""
    if len(text) % 2:
        text = text[:-1]
    try:
        return bytes.fromhex(text)
    except ValueError:
        return b""


class Xfer:
    """One I2C transfer: a request paired with the response it got."""

    def __init__(self, req):
        self.req = req
        self.resp = None
        self.kind = "W" if req.kind == "i2c.req" else "R"
        self.addr = req.addr()
        self.time = req.time
        self.stop = req.fields.get("stop") == "1"
        self.rs = "rs" in req.flags
        self.data = hexbytes(req.fields.get("data")) if self.kind == "W" else b""
        self.want = int(req.fields.get("req", "0") or 0) if self.kind == "R" else 0
        self.status = None
        self.got = b""
        self.got_len = 0
        self.truncated = False
        self.fold = req.fold

    def answer(self, resp):
        self.resp = resp
        if self.kind == "W":
            self.status = int(resp.fields.get("status", "255") or 255)
        else:
            self.got_len = int(resp.fields.get("len", "0") or 0)
            self.got = hexbytes(resp.fields.get("data"))
            self.truncated = len(self.got) < self.got_len
            if len(self.got) > self.got_len:
                self.got = self.got[:self.got_len]

    def label(self):
        if self.kind == "W":
            if len(self.data) >= 2:
                return "write %02X=%s" % (self.data[0], self.data[1:].hex().upper())
            if len(self.data) == 1:
                return "pointer %02X" % self.data[0]
            return "probe"
        ptr = getattr(self, "pointer", None)
        return "read %02X" % ptr if ptr is not None else "read"


def pair_transfers(events):
    xfers = []
    pending = {}
    open_w = []
    open_r = []
    for e in events:
        if e.kind in ("i2c.req", "i2c.rd.req"):
            x = Xfer(e)
            xfers.append(x)
            if e.seq is not None:
                pending[e.seq] = x
            (open_w if x.kind == "W" else open_r).append(x)
        elif e.kind in ("i2c.resp", "i2c.rd.resp"):
            want = "W" if e.kind == "i2c.resp" else "R"
            x = pending.pop(e.link, None) if e.link is not None else None
            if x is None or x.kind != want:
                queue = open_w if want == "W" else open_r
                x = next((q for q in queue if q.resp is None), None)
            if x is not None:
                x.answer(e)
    return [x for x in xfers if x.resp is not None]


class Obs:
    def __init__(self, kind, xfer, payload):
        self.kind = kind  # 'R' or 'W'
        self.xfer = xfer
        self.time = xfer.time
        self.payload = payload
        self.truncated = xfer.truncated if kind == "R" else False
        self.full_len = xfer.got_len if kind == "R" else len(payload)

    def hex(self):
        text = self.payload.hex().upper()
        if self.truncated:
            text += ".."
        return text


class Reg:
    def __init__(self, address):
        self.address = address
        self.width = 0
        self.writable = False
        self.obs = []
        self.refused = []
        self.short_reads = []
        self.volatile = False
        self.partial = False
        self.reset = b""
        self.notes = []


class Analysis:
    def __init__(self, address):
        self.address = address
        self.regs = {}
        self.absent = {}
        self.pointer_refused = []
        self.todo = []
        self.remarks = []
        self.channels = {}
        self.channel_notes = []
        self.lines = []
        self.notes = []
        self.diags = []
        self.require_rs = False
        self.rs_basis = ""

    def reg(self, address):
        return self.regs.setdefault(address, Reg(address))


def describe_between(xfers, chans, lo, hi, own):
    """What happened to this device between two event indexes."""
    writes = [x for x in xfers if lo < x.req.index < hi and x.kind == "W"
              and len(x.data) >= 2 and x.status == 0]
    other_writes = [x for x in writes if x.data[0] != own]
    own_writes = [x for x in writes if x.data[0] == own]
    reads = [x for x in xfers if lo < x.req.index < hi and x.kind == "R"
             and x.got_len > 0 and x.pointer != own]
    chan = [c for c in chans if lo < c.index < hi]
    return own_writes, other_writes, reads, chan


def analyze(events, address):
    a = Analysis(address)
    xfers = [x for x in pair_transfers(events) if x.addr == address]
    chans = [e for e in events if e.kind == "chan.write"]
    pointer = None
    for x in xfers:
        if x.kind == "W":
            if len(x.data) == 0:
                continue
            pointer = x.data[0]
            if len(x.data) == 1:
                if x.status != 0:
                    a.pointer_refused.append(x)
                continue
            reg = a.reg(pointer)
            payload = x.data[1:]
            if x.status == 0:
                reg.writable = True
                reg.width = max(reg.width, len(payload))
                reg.obs.append(Obs("W", x, payload))
            else:
                reg.refused.append(x)
        else:
            if pointer is None:
                pointer = 0
                a.remarks.append(
                    "a read came before any pointer write; assumed pointer 00")
            x.pointer = pointer
            if x.got_len == 0:
                a.absent.setdefault(pointer, []).append(x)
                continue
            reg = a.reg(pointer)
            reg.width = max(reg.width, x.got_len)
            reg.obs.append(Obs("R", x, x.got))
            if x.got_len < x.want:
                reg.short_reads.append(x)
    for x in xfers:
        if x.kind == "R" and not hasattr(x, "pointer"):
            x.pointer = None

    # Repeated start: required iff every data-returning read continued a
    # pointer write. Both choices reproduce such a capture; requiring it
    # is the stricter one and is what most register-map parts do.
    data_reads = [x for x in xfers if x.kind == "R" and x.got_len > 0]
    if data_reads:
        a.require_rs = all(x.rs for x in data_reads)
        with_rs = sum(1 for x in data_reads if x.rs)
        a.rs_basis = "%d of %d data-returning reads continued a pointer write" % (
            with_rs, len(data_reads))
    else:
        a.rs_basis = "no data-returning read seen"

    # Reads that returned nothing: refused for lack of a repeated start, or
    # a register the part does not have.
    for ptr, reads in sorted(a.absent.items()):
        if ptr in a.regs:
            continue
        if a.require_rs and any(not x.rs for x in reads):
            a.remarks.append(
                "register %02X: read without repeated start returned nothing "
                "(consistent with requiring it)" % ptr)
        else:
            a.remarks.append(
                "register %02X: read returned nothing %dx — no such register, "
                "or no answer; left out of the table" % (ptr, len(reads)))

    for reg in sorted(a.regs.values(), key=lambda r: r.address):
        first = reg.obs[0] if reg.obs else None
        first_read = next((o for o in reg.obs if o.kind == "R"), None)
        if first is not None and first.kind == "R":
            reg.reset = bytes(first.payload) + bytes(reg.width - len(first.payload))
            if first.truncated:
                reg.partial = True
                a.todo.append(
                    "register %02X: %d bytes read, only %d recorded in the log — "
                    "contents beyond that are unknown (zeros)" %
                    (reg.address, first.full_len, len(first.payload)))
            own_w, other_w, other_r, chan = describe_between(
                xfers, chans, -1, first.xfer.req.index, reg.address)
            causes = ["write %02X=%s" % (x.data[0], x.data[1:].hex().upper())
                      for x in other_w]
            causes += ["chan.write %s=%s" % (c.fields.get("chan"),
                                             c.fields.get("data"))
                       for c in chan]
            if causes:
                reg.notes.append("first read came after %s: power-on contents "
                                 "uncertain" % ", ".join(causes))
        else:
            reg.reset = bytes(reg.width)
            if first_read is None:
                reg.notes.append("never read: power-on contents unknown (zeros)")
            else:
                reg.notes.append("first read came after a write to it: power-on "
                                 "contents unknown (zeros)")

        reads = [o for o in reg.obs if o.kind == "R"]
        for prev, cur in zip(reads, reads[1:]):
            n = min(len(prev.payload), len(cur.payload))
            if prev.payload[:n] == cur.payload[:n]:
                continue
            own_w, other_w, other_r, chan = describe_between(
                xfers, chans, prev.xfer.req.index, cur.xfer.req.index,
                reg.address)
            if own_w:
                continue  # explained by a write to this register
            elapsed = (cur.time or 0) - (prev.time or 0)
            causes = []
            if other_w:
                causes += ["after write %02X=%s" % (x.data[0],
                                                    x.data[1:].hex().upper())
                           for x in other_w]
            if other_r:
                causes += ["after read of %02X" % x.pointer for x in other_r]
            if chan:
                causes += ["after chan.write %s" % c.fields.get("chan")
                           for c in chan]
            if not causes:
                cause = "with only time passing (%d us)" % elapsed
            else:
                cause = "%s (%d us)" % (", ".join(causes), elapsed)
            reg.volatile = True
            a.todo.append("register %02X changed %s -> %s %s" %
                          (reg.address, prev.hex(), cur.hex(), cause))
        for x in reg.short_reads:
            a.todo.append("register %02X answered %d bytes to a %d-byte read: the "
                          "length is device state, not the table's width" %
                          (reg.address, x.got_len, x.want))
        for x in reg.refused:
            reg.notes.append("write %s refused with status %d" %
                             (x.data[1:].hex().upper(), x.status))

    for x in a.pointer_refused:
        a.remarks.append("pointer write %02X refused with status %d: the table "
                         "acknowledges every pointer" % (x.data[0], x.status))

    # World channels: a payload that reads back verbatim from one register
    # can be a table mapping; anything else is a transformation.
    for c in chans:
        chan = c.fields.get("chan")
        payload = hexbytes(c.fields.get("data"))
        hits = []
        for reg in a.regs.values():
            for o in reg.obs:
                if (o.kind == "R" and o.xfer.req.index > c.index
                        and not o.truncated and o.payload == payload):
                    hits.append(reg.address)
                    break
        if len(hits) == 1:
            if chan not in a.channels:
                a.channels[chan] = hits[0]
                a.channel_notes.append(
                    "chan %s -> register %02X (payload %s read back verbatim)" %
                    (chan, hits[0], payload.hex().upper()))
            continue
        if len(hits) > 1:
            a.todo.append("chan %s payload %s reads back from several registers "
                          "(%s): choose one" %
                          (chan, payload.hex().upper(),
                           ", ".join("%02X" % h for h in hits)))
            continue
        inside = sorted({reg.address for reg in a.regs.values()
                         for o in reg.obs
                         if o.kind == "R" and payload and payload in o.payload
                         and o.payload != payload})
        hint = (" (appears inside register %s)" %
                ", ".join("%02X" % h for h in inside)) if inside else ""
        a.todo.append("chan %s payload %s appears in no register verbatim%s: "
                      "a hook must transform it" %
                      (chan, payload.hex().upper(), hint))

    # Lines, notes, diagnostics: nothing a table can express.
    for i, e in enumerate(events):
        if e.kind == "gpio.inject":
            line = e.fields.get("line", e.fields.get("pin"))
            level = e.fields.get("val")
            if level is None:
                for tok in e.flags:
                    em = EDGE_RE.match(tok)
                    if em:
                        level = em.group(2)
            prev = events[i - 1] if i > 0 else None
            while prev is not None and prev.kind.startswith("diag."):
                prev = events[prev.index - 1] if prev.index > 0 else None
            cause = ""
            if prev is not None and prev.kind == "chan.write":
                cause = "right after chan.write chan=%s" % prev.fields.get("chan")
            elif prev is not None and prev.kind in ("i2c.req", "i2c.rd.req"):
                x = next((t for t in xfers if t.req is prev), None)
                cause = "during a %s" % (x.label() if x else prev.kind)
            else:
                # Anchor on the last command (a write) as well as on the last
                # transfer of any kind: a latency counts from the command.
                last = None
                last_write = None
                for x in xfers:
                    if x.req.index >= e.index:
                        break
                    last = x
                    if x.kind == "W" and len(x.data) >= 2 and x.status == 0:
                        last_write = x
                parts = []
                if last_write is not None:
                    parts.append("%d us after %s" % (
                        (e.time or 0) - (last_write.time or 0), last_write.label()))
                if last is not None and last is not last_write:
                    parts.append("%d us after %s" % (
                        (e.time or 0) - (last.time or 0), last.label()))
                cause = ", ".join(parts) if parts else "no transfer before it"
                if e.ctx == "tick":
                    cause += " (on the device's own schedule)"
            a.lines.append((e.time, line, level, cause))
            a.todo.append("line %s -> %s at %s us: %s" %
                          (line, level, e.time, cause))
        elif e.kind == "dev.note":
            a.notes.append((e.time, e.rest))
        elif e.kind.startswith("diag."):
            a.diags.append((e.time, e.kind + (" " + e.rest if e.rest else "")))
    if a.notes:
        a.todo.append("%d device note(s) in the capture — commentary the part "
                      "gave; decide whether the model should say the same" %
                      len(a.notes))
    return a


def cpp_ident(name):
    ident = re.sub(r"\W", "", name)
    if not ident or not (ident[0].isalpha() or ident[0] == "_"):
        ident = "T" + ident
    return ident


def render_header(a, name, source):
    ident = cpp_ident(name)
    out = []
    out.append("// Generated by devices/tools/trace2regtable.py from %s" % source)
    out.append("// (device address 0x%02X). A scaffold: the table is what the"
               % a.address)
    out.append("// capture showed, the TODO list is what it could not. Put hooks")
    out.append("// in a class derived from %sTable, not here — this file is" % ident)
    out.append("// regenerated when the capture changes.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <regtable_model.h>")
    out.append("")
    regs = sorted(a.regs.values(), key=lambda r: r.address)
    for reg in regs:
        obs = ", ".join("%s %s @%s" % ("read" if o.kind == "R" else "wrote",
                                       o.hex(), o.time) for o in reg.obs)
        out.append("// 0x%02X: %d byte%s; %s" % (reg.address, reg.width,
                                                 "" if reg.width == 1 else "s",
                                                 obs))
        for note in reg.notes:
            out.append("//       %s" % note)
        out.append("static const uint8_t k%sReset_%02X[%d] = {%s};" % (
            ident, reg.address, reg.width,
            ", ".join("0x%02X" % b for b in reg.reset)))
    out.append("")
    out.append("static const RegTableEntry k%sEntries[%d] = {" % (ident, len(regs)))
    for reg in regs:
        flags = []
        if reg.writable:
            flags.append("RegTableModel::kWritable")
        if reg.volatile:
            flags.append("RegTableModel::kVolatile")
        if reg.partial:
            flags.append("RegTableModel::kPartial")
        out.append("    {0x%02X, %d, k%sReset_%02X, %s}," % (
            reg.address, reg.width, ident, reg.address,
            " | ".join(flags) if flags else "0"))
    out.append("};")
    if a.channels:
        out.append("static const RegTableChannel k%sChannels[%d] = {" % (
            ident, len(a.channels)))
        for chan, addr in sorted(a.channels.items()):
            out.append("    {%s, 0x%02X}," % (chan, addr))
        out.append("};")
        chan_ref = "k%sChannels, %d" % (ident, len(a.channels))
    else:
        chan_ref = "nullptr, 0"
    out.append("static const RegTableSpec k%sSpec = {" % ident)
    out.append("    k%sEntries, %d, %s, %s};" % (
        ident, len(regs), chan_ref, "true" if a.require_rs else "false"))
    out.append("")
    if a.todo:
        out.append("// The table alone reproduces the constant answers. Each item")
        out.append("// below needs a hook (onRead, afterWrite, advanceTo,")
        out.append("// channelWrite) in a derived class:")
        for item in a.todo:
            out.append("//   - %s" % item)
    else:
        out.append("// Nothing in the capture is beyond the table.")
    out.append("class %sTable : public RegTableModel {" % ident)
    out.append(" public:")
    out.append("  %sTable() : RegTableModel(k%sSpec) {}" % (ident, ident))
    out.append("};")
    out.append("")
    return "\n".join(out)


def render_report(a, name, source):
    out = []
    out.append("trace2regtable report — %s" % name)
    out.append("source: %s" % source)
    out.append("device: address 0x%02X" % a.address)
    out.append("")
    out.append("registers")
    for reg in sorted(a.regs.values(), key=lambda r: r.address):
        out.append("  0x%02X width=%d %s reset=%s" % (
            reg.address, reg.width, "rw" if reg.writable else "ro",
            reg.reset.hex().upper() or "-"))
        for o in reg.obs:
            out.append("      %s %s @%s us%s" % (
                "read " if o.kind == "R" else "wrote", o.hex(), o.time,
                " (rs)" if o.kind == "R" and o.xfer.rs else ""))
        for note in reg.notes:
            out.append("      ! %s" % note)
    out.append("")
    out.append("repeated start: %s (%s)" % (
        "required" if a.require_rs else "not required", a.rs_basis))
    out.append("")
    out.append("channels")
    for note in a.channel_notes:
        out.append("  %s" % note)
    if not a.channel_notes:
        out.append("  none mapped")
    out.append("")
    out.append("lines")
    for time, line, level, cause in a.lines:
        out.append("  line %s -> %s at %s us: %s" % (line, level, time, cause))
    if not a.lines:
        out.append("  none")
    out.append("")
    out.append("device notes")
    for time, text in a.notes:
        out.append("  @%s us: %s" % (time, text))
    if not a.notes:
        out.append("  none")
    out.append("")
    out.append("diagnostics")
    for time, text in a.diags:
        out.append("  @%s us: %s" % (time, text))
    if not a.diags:
        out.append("  none")
    out.append("")
    out.append("remarks")
    for r in a.remarks:
        out.append("  %s" % r)
    if not a.remarks:
        out.append("  none")
    out.append("")
    out.append("needs a hook (%d)" % len(a.todo))
    for item in a.todo:
        out.append("  - %s" % item)
    out.append("")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("trace", type=Path)
    ap.add_argument("--name", required=True,
                    help="C++ identifier stem: <Name>Table, k<Name>Spec")
    ap.add_argument("--out", type=Path, required=True, help="header to write")
    ap.add_argument("--report", type=Path, help="text report to write")
    ap.add_argument("--addr", help="device address (hex); needed when the "
                    "trace shows more than one")
    args = ap.parse_args(argv)

    events = parse(args.trace.read_text().splitlines())
    addresses = sorted({x.addr for x in pair_transfers(events)
                        if x.addr is not None})
    if args.addr is not None:
        address = int(args.addr, 16)
    elif len(addresses) == 1:
        address = addresses[0]
    else:
        sys.exit("trace shows %d device addresses (%s); pass --addr" % (
            len(addresses), ", ".join("%02X" % x for x in addresses)))
    a = analyze(events, address)
    source = args.trace.name
    args.out.write_text(render_header(a, args.name, source))
    if args.report:
        args.report.write_text(render_report(a, args.name, source))
    writable = sum(1 for r in a.regs.values() if r.writable)
    print("%s: %d registers (%d writable), %d channel(s) mapped, %d item(s) "
          "need a hook" % (args.name, len(a.regs), writable, len(a.channels),
                           len(a.todo)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
