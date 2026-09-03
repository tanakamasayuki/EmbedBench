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

The current `smoke/` test is the minimal end-to-end check for Arduino library
resolution, host core 1.7.0, lifecycle hooks, the virtual clock, and pytest
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
