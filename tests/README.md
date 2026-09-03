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
