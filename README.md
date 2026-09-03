# EmbedBench

[日本語](README.ja.md)

EmbedBench is an Arduino library and test bed for designing host-side embedded
application verification through experiments. Its public API and final
architecture are intentionally not fixed yet.

Unsettled design records are kept in Japanese under
[docs/README.ja.md](docs/README.ja.md). User-facing documentation is provided
in linked English and Japanese versions.

The repository currently contains only a minimal runnable environment:

- a valid Arduino library under `src/`;
- `pytest-embedded` tests running on `lang-ship:host` 1.7.0;
- a smoke sketch exercising lifecycle and virtual-clock extension ports.

## Tests

Every test runs on the host through the `socket://localhost` port selected by
its `sketch.yaml`.

```sh
cd tests
uv sync
uv run pytest -v -s
```

See [tests/README.md](tests/README.md) for the test layout.
