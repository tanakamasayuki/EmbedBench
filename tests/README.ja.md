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

追加の実験:

- `clock/`: Arduinoの各待ちAPIがclock portを呼ぶ回数と長さ
- `lifecycle/`: setupとloopを囲むライフサイクルの実順序
- `ports/`: GPIO・Analog・SPI・Wire・UARTの観測・注入・応答
- `tick_split/`: 任意長のwaitを固定tick境界へ分割する候補
- `hook_slots/`: host coreの単一hook登録を置き換えたときの挙動
- `host_gaps/`: 割り込み・Analog mV読取り・UART観測で不足する拡張点
- `zero_wait/`: 0 us waitを外部処理の機会にした場合の再入と無限ループ条件
- `tick_guard/`: tick callback内から別の待ちAPIを呼んだ場合の3方式比較
- `listener_fanout/`: 単一hookから固定slotの複数listenerへの配送と上限・callback中解除
- `event_buffer/`: イベント記録のサイズ、固定バッファ満杯方式、1行形式3候補の容量
- `wire_split/`: Wireの観測者と応答デバイスを分離して戻り値を一意にする構造
- `device_route/`: 全直接・全共通IF・仕込み/実行/検分分離の3案を同一模型で比較（WP-A2）
