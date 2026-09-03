# host-arduino-core拡張点の監査

内部の記録。日本語のみ。host-arduino-core 1.7.0だけでEmbedBenchの候補範囲を
実現できるかを、ソースと`tests/host_gaps/`の実測から整理する。

## 1. 結論

最初のI2C縦切り、仮想時計、ライフサイクル、GPIO/SPI/Wire/Analog rawの観測、
上位でのhook多重化は**追加依頼なしで開始できる**。

ただし、次を最終的な対象へ含めるならhost coreへの追加依頼が必要になる。

| 優先度 | 不足 | 影響 |
| --- | --- | --- |
| P0 | GPIO interruptの登録保持・callback呼出し口 | `attachInterrupt`がno-opのため、EmbedBenchがedgeを検出しても登録ISRを呼べない |
| P0 | device UARTの同期activity hook | TXを後からqueue pollingするため、書込み時刻と他イベントとの順序を失う。waitしない即時応答もできない |
| P1 | `analogReadMilliVolts`のread hook | mV読取りだけ観測・応答を差し替えできない |
| P1 | UART begin/end/config/readの通知 | UART設定と受信消費を完全なイベント列へ載せられない |

P0はGate Aと並行して早めに依頼する価値がある。P1は依頼へ含めてよいが、最初の
I2C縦切りを止めない。FreeRTOSと`esp_timer`は標準的なArduino実行モデルの
外とし、今回の監査・実験・追加依頼から除外する。

## 2. 能力表

| 領域 | 観測 | 応答・注入 | 判定 |
| --- | --- | --- | --- |
| lifecycle | setup/loop前後の1 hook | — | 十分。多重化はEmbedBench側 |
| clock | now/waitの差替え | 仮想時刻を提供可能 | 単一loop用途は十分 |
| GPIO mode/write | 同期hookあり | — | 十分 |
| GPIO read | read hookで結果を返せる | `setPinValue`あり | 十分 |
| GPIO interrupt | attach/detachの通知・保持なし | callback呼出し口なし | 不足 |
| Analog raw read | read hookあり | `setAnalogValue`あり | 十分 |
| Analog mV read | hookなし | `setAnalogMilliVolts`はある | 観測が不足 |
| PWM/DAC/tone | 状態付きwrite hookあり | — | 波形以外は十分 |
| SPI | lifecycle/transaction/byte hook | byte戻り値で応答 | 十分 |
| Wire | lifecycle/transaction hook | write status/read bytesを返せる | 十分 |
| UART | tx/rx queueと最終状態 | pollingと`pushRx` | 発生時刻・即時応答が不足 |
| console Serial | socket/stdout | device用ではない | 対象外候補 |

## 3. 実測

`tests/host_gaps/`の結果:

```text
interrupt_edges=2 callback_calls=0
analog_raw=1234 mv=3300 hook_after_raw=1 hook_after_mv=1
uart_tx_delta=2 queued=2 drained=2 bytes=AT activity_hook=0
```

- GPIO値をLOW/HIGHへ2回変えても、登録したISR callbackは0回
- raw ADC読取り後はhook 1回、続くmV読取り後も1回のまま
- UARTの2byteはqueueに残り後から取得できるが、発生時のactivity hookは存在しない

## 4. EmbedBench側だけで解決できるもの

- host coreの単一hookを1つずつ所有し、複数listener/deviceへ配送する
- イベント受付時のsequence/timestamp付与
- `setPinValue`、`setAnalogValue`、`pushRx`をEmbedBench経由にして注入を記録する
- 任意長waitを固定tick境界へ分割する
- I2C/SPIの観測とdevice応答を分離する
- attach表、channel、dump、golden、固定バッファ

これらをhost coreへ入れると検証Protocol固有の責務が漏れるため、追加依頼しない。

## 5. 追加依頼案

### H1 — GPIO interruptの登録保持・callback呼出し口（P0）

線の変化、edge判定、発火順序を決めるのはEmbedBench側とする。host coreには
Arduino APIが受け取った登録情報を保持し、EmbedBenchから指定されたcallbackを
呼び出す最小限の仕組みだけを求める。

要求する性質:

- `attachInterrupt` / `detachInterrupt`がpin、mode、callbackを保持する
- 外部コードがpinを指定して、登録済みcallbackを同期呼出しできる
- attach/detachを観測できる単一の汎用hookまたは同等の口がある
- 何も登録しない既存sketchの挙動を変えない
- edge判定、保留FIFO、ネスト禁止などEmbedBench固有の方針はhost coreへ入れない

EmbedBenchは自分のGPIO注入経路で線の変化を捕捉し、登録modeと照合し、
イベントのsequence/timestampを確定した後にhostのcallback呼出し口を呼ぶ。
host coreが`setPinValue`の変化から自動的にedge判定する仕様は求めない。

この分担なら、runtime中の外部GPIO変化をEmbedBench経由に限定することで、
値変更、ログ、edge判定、ISR呼出しの順序を1か所で管理できる。
`HostArduino::setPinValue()`を直接呼び出す経路はこの保証対象外とする。

### H2 — device UARTの同期activity port（P0）

要求する性質:

- begin/end、baud/config/pin変更、受理されたTX byte列、sketchが消費したRX byte列を観測できる
- TX通知は`write()`が戻る前に発生し、他のGPIO/SPIイベントとの順序を保存できる
- TX通知から安全にRXを積める。callbackは内部mutexを保持したまま呼ばない
- port番号と実際に受理・消費されたbyte数が分かる
- 単一slotでよく、多重化は上位が担当する
- baud timingや物理波形は模擬しない

### H3 — Analog read観測の穴を塞ぐ（P1）

要求する性質:

- `analogReadMilliVolts`も読取りhookを通り、held値と最終結果を扱える
- 既存のraw `AnalogReadHook`を壊さない
- 可能ならread resolution/width変更も順序付きで観測できる
- attenuationやVrefをhost coreが推測しない

## 6. 進行への影響

H1/H2の回答を待たず、Gate Aの操作経路表とI2C縦切りは進められる。ただし、
interrupt/UARTの公開仕様とgolden形式は、追加口が確定するまで凍結しない。
