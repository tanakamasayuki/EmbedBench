# Tests

> 日本語: [README.ja.md](README.ja.md)

EmbedBench experiments and automated tests use the same
`pytest-embedded` and Arduino CLI structure as TinyGFX.

## Running the tests

```sh
uv sync
uv run pytest -v -s
```

Each `sketch.yaml` selects `socket://localhost` directly. There are no hardware
profiles or environment files.

Every experiment has its own directory:

```text
tests/<experiment>/
  <experiment>.ino
  sketch.yaml
  test_<experiment>.py
```

Native-only experiments (`native_env/`) have no `.ino` or `sketch.yaml`; pytest
builds them directly with g++. Reference models shared across experiments and
environments live in `common_models/` (Arduino library layout, pure C++ models
under `src/`), referenced by `libraries: dir` from sketches and by `-I` natively.

The current `smoke/` test is the minimal end-to-end check for Arduino library
resolution, host core 1.7.1, lifecycle hooks, the virtual clock, and pytest
connectivity.

Additional experiments:

- `clock/`: call counts and slice lengths for Arduino waiting APIs;
- `lifecycle/`: actual lifecycle order around setup and loop;
- `ports/`: GPIO, analog, SPI, Wire, and UART observation and response paths;
- `tick_split/`: a candidate that divides arbitrary waits at fixed tick boundaries.
- `hook_slots/`: replacement behavior of the host core's single hook slots.
- `host_gaps/`: missing extension points for interrupts, analog millivolt reads, and UART observation.
- `zero_wait/`: re-entry and infinite-loop conditions when 0 us waits drive external processing.
- `tick_guard/`: three policies for a wait API called from inside a tick callback.
- `listener_fanout/`: fanning one host hook out to fixed listener slots, the cap, and removal during callbacks.
- `event_buffer/`: event record size, fixed-buffer overflow policies, and the byte cost of three line formats.
- `wire_split/`: splitting Wire into observers plus a single responder per address for unique return values.
- `device_route/`: comparing all-direct, all-common-interface, and setup/run/inspect-split routing with one device model (WP-A2).
- `interrupt_port/`: host core 1.7.1 interrupt port (registration observation, synchronous invocation, enter/exit, mode normalization).
- `uart_activity/`: host core 1.7.1 UART activity hook (synchronous TX, in-hook immediate replies, discard reporting).
- `analog_mv/`: host core 1.7.1 millivolt read hook and read-width configuration observation.
- `interrupt_slice/`: vertical slice from line injection through edge decision, ISR invocation, and ctx=isr tagging.
- `uart_golden/`: AT conversation golden with immediate and tick-delayed replies on the virtual clock.
- `i2c_slice/`: the WP-C1 I2C vertical slice (unmodified app, zero event loss, 3x reproducible).
- `reject_paths/`: how host-rejected or dropped operations appear to the current hooks (for the completeness-scope decision).
- `event_timing/`: ordering comparison of four event-completion policies under a re-entrant sink call from a response callback.
- `uart_sink/`: X17 redone with device replies recorded through the core RX sink so device send times enter the log.
- `core_draft/`: the integrated draft core (`src/embedbench_draft.*`) putting GPIO, an interrupt, I2C, UART, and time on one event list.
- `device_if/`: the device interface being fixed (`src/embedbench_device.h`): a pure-C++ g++-only build and the same unmodified sources running on the host core.
- `spi_device/`: a composite device (SPI plus a DC input line, a busy output line, and time) on the same interface; lineIn was the only addition.
- `frame_port/`: the generic frame path (format id plus pre-encoding logical bits) for protocols without a dedicated port.
- `format_registry/`: four format-identity schemes compared (fixed-number collision, environment-interned names, strings only, no-registry degradation) plus frame bus ids.
- `capacity/`: negotiated size limits (`maxFrameBits`): one model auto-splitting per environment, oversize rejected visibly.
- `bulk_spi/`: bulk-transfer recording granularity: in-transaction count+checksum summaries vs the per-byte buffer explosion.
- `native_env/`: environment example #2, a pure-C++ minimal recorder replaying the X23 scenario with the same `common_models/` sources and matching device-side event lines.
- `i2c_transaction/`: I2C transaction context (STOP, repeated start) with a register-map model that requires repeated start.
- `frame_bits/`: frame bit packing (MSB-first, padding check, empty frames) and atomicity (a 128-bit frame is refused, never split).
- `reentry/`: the re-entrancy rule: a model raising IRQ mid-response, immediate delivery (depth 2) vs deferred delivery (depth 1).
- `contracts/`: channel / dump / time contracts (native only): return values, NUL termination, repeated time, jumps, reset.
- `format_schema/`: format names with schema fingerprints: same name+schema idempotent, same name different schema diagnosed.
- `common_models/`: reference models (temperature sensor, AT modem) shared across experiments and environments.
