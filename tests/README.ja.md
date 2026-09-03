# テスト

> English: [README.md](README.md)

EmbedBench の実験と自動テストは、TinyGFX と同じ
`pytest-embedded` + Arduino CLI の構成を使う。

## 実行

```sh
uv sync
uv run pytest -v -s
```

各 `sketch.yaml` にある `socket://localhost` を使う。実機用プロファイルや
環境ファイルは持たない。

各実験は独立したディレクトリに置く。

```text
tests/<実験名>/
  <実験名>.ino
  sketch.yaml
  test_<実験名>.py
```

現在の `smoke/` は、Arduinoライブラリとしての解決、host core 1.7.0、
ライフサイクルフック、仮想時計、pytestとの接続をまとめて検査する最小テストである。
