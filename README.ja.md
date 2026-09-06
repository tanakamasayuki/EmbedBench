# EmbedBench

[English](README.md)

組込みアプリケーションをホスト上で検証する仕組みを、実験しながら設計するための
Arduinoライブラリです。公開APIと最終的な構成はまだ確定していません。

検証するのは「アプリケーションが**何をしようとしたか**」です。
「0x76番地へ0xF4を書いて、7.5 ms待って、3バイト読んだ」までが対象で、
仮想時計とデバイス模型の上で動かし、1行1イベントで記録します。
物理層（波形、タイミングマージン）は実機でしか確かめられないので範囲外です。

## ドキュメント

- **[入門ガイド](docs/GUIDE.ja.md)** — 何のためのものか、記録の読み方、
  最初のテストの書き方
- **[上級ガイド](docs/ADVANCED.ja.md)** — デバイス模型の書き方、守るべき契約、
  実装例を作る過程で踏んだ落とし穴

設計記録・実験台帳・デバイスカタログは日本語のみで
[docs/README.ja.md](docs/README.ja.md) 以下に置きます。

現在の内容:

- Arduinoライブラリとしての `src/`。凍結済みデバイスIF
  （`src/embedbench_device.h`、version 1 / revision 004）と環境実装例2種を含む
- `devices/` の共有デバイス模型22種。温度計からModbusスレーブ、
  UWBアンカー、わざと壊れる部品まで
- `lang-ship:host` 1.7.1 上で動く実験62件（`pytest-embedded` テスト78件）

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
