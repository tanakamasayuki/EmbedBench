# EmbedBench

[日本語](README.ja.md)

EmbedBench is an Arduino library and test bed for designing host-side embedded
application verification through experiments. Its public API and final
architecture are intentionally not fixed yet.

It verifies **what the application was trying to do** — "write 0xF4 to address
0x76, wait 7.5 ms, read three bytes" — on a virtual clock, against device
models, with one line recorded per event. The physical layer is deliberately
out of scope: waveforms and timing margins can only be confirmed on real
hardware.

## Documentation

- **[Getting started](docs/GUIDE.md)** — what it is for, how to read a trace,
  how to write your first test.
- **[Current state, in numbers](docs/FACTS.md)** — figures other documents can
  cite, kept true by a test.
- **[Advanced guide](docs/ADVANCED.md)** — writing device models, the
  contracts they must keep, and the traps found while building the examples.

Design records, the experiment ledger and the device catalog are kept in
Japanese under [docs/README.ja.md](docs/README.ja.md).

The repository currently contains:

- a valid Arduino library under `src/`, including the frozen device interface
  (`src/embedbench_device.h`, version 1 / revision 004) and two example
  environment implementations;
- shared device models under `devices/`, from a thermometer to a Modbus
  slave, a UWB anchor, an SD card and a part that misbehaves on demand;
- the board-side capture shims under `capture/` (`CaptureWire`,
  `CaptureSerial`, `CaptureSPI`, `CaptureLines`), which record a session
  on real hardware — bus traffic and the lines a part drives — in the
  lines the scaffold generator (`devices/tools/trace2regtable.py`) and
  the tape generator (`trace2tape.py`) read
- experiments running on `lang-ship:host` 1.7.1 (counts in
  [docs/FACTS.md](docs/FACTS.md)).

## Tests

Every test runs on the host through the `socket://localhost` port selected by
its `sketch.yaml`.

```sh
cd tests
uv sync
uv run pytest -v -s
```

See [tests/README.md](tests/README.md) for the test layout.
