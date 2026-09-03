# EmbedBench

[English](README.md)

組込みアプリケーションをホスト上で検証する仕組みを、実験しながら設計するための
Arduinoライブラリです。公開APIと最終的な構成はまだ確定していません。

設計中の記録は日本語のみで [docs/README.ja.md](docs/README.ja.md) 以下に置きます。
利用者向け文書は英語版と日本語版を相互リンクします。

現在は次の最小環境だけを置いています。

- Arduinoライブラリとして解決・コンパイルできる最小の `src/`
- `lang-ship:host` 1.7.0 上で動く `pytest-embedded` テスト
- ライフサイクルフックと仮想時計を同時に検証する smoke sketch

## テスト

各 `sketch.yaml` の `socket://localhost` を使い、すべてhost上で実行します。

```sh
cd tests
uv sync
uv run pytest -v -s
```

テストは `tests/<実験名>/` ごとに次の3ファイルを置く形です。
詳細は [tests/README.ja.md](tests/README.ja.md) を参照してください。

```text
<実験名>.ino
sketch.yaml
test_<実験名>.py
```
