# 操作経路表（WP-A1）

内部の記録。日本語のみ。Gate Aの承認材料であり、承認前の**候補**である。
host-arduino-core 1.7.1のhook一覧と、[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md)の
X0〜X19の実測に基づいて、外部から観測可能な操作のすべてに記録点と応答担当を
割り当てる。「この操作はログに残らない」を暗黙に残さないための表である。
1.7.0時点で不足していた割り込み・UART・mV読取りの記録点は、1.7.1の追加口
（[HOST_EXTENSION_AUDIT.ja.md](HOST_EXTENSION_AUDIT.ja.md)のH1〜H3、検証X13〜X15）で埋まった。
hostが**受理せず捨てる**呼び出しがhookに一切届かないことはX19で実測した。
その扱いは未決2として本表の最後にまとめる。

## アクターの凡例

| 記号 | アクター | 責務の候補 |
| --- | --- | --- |
| app | Arduinoアプリ | 無改造。Arduino APIだけを呼ぶ |
| core | EmbedBench核 | host coreの各単一hookを1枠ずつ所有し、受付時にsequence/timestampを付与して記録し、listenerとデバイスへ配送する（X5, X9） |
| dev | デバイス模型 | アドレス・pin・busにbindされ、応答値を一意に決める（X11）。**外向きの作用（GPIO・Analog・UART RX）は必ずcoreのsink経由** |
| dir | 進行役（テスト側） | 注入と時間の進行。すべての注入はcore経由 |
| diag | 診断 | 上限超過・未bind・満杯などをイベントとして残す（X9〜X11） |

## 経路の原則（候補）

1. host coreのhookはすべてcoreが1枠ずつ所有する。利用者・デバイスがhost coreへ
   直接登録する構成は成立しない（X5）
2. sequence/timestampはcoreの受付点で付与する。**応答を伴う操作（GPIO/Analog read、
   SPI transfer、I2C read/write）でイベントをいつ完成させるかは未決1**。
   「記録してから応答」を原則に確定すると、応答値を同じイベントへ含めることと
   両立せず、応答callback中の再入イベントの順序にも影響するため、
   完成タイミングの決定（未決1）まで原則にしない
3. 走行中の外部状態変更は、**dirとdevのどちらが起こすものも**coreのsink
   （GPIO / Analog / UART RX / channel）経由で記録してから反映する。
   デバイスのIRQ線・DRDY pin・UART応答もこの経路に載せる。`setPinValue` や
   `pushRx` などhost core APIの直接呼び出しは保証対象外とする（監査H1の分担と同じ）
4. 対象busは標準global instance（`SPI` / `Wire` / `Wire1` / `Serial1` / `Serial2`）
   に限る。アプリが生成した独自instanceは無改造のまま自動発見できないため対象外
   とし、必要になったらテスト側の明示登録で追加する（候補）

## アクター図（テキスト、候補）

```text
             (1) 操作              (2) 単一hook           (3) 受付・記録
アプリ ──Arduino API──→ host中間層 ─────────────→ EmbedBench核
                                                  （sequence/timestamp付与）
                                                        │
                                        ┌───────────────┼───────────────┐
                                        ↓               ↓               ↓
                                   listener群      デバイス模型      診断イベント
                                  （観測のみ、    （応答値を一意に
                                   戻り値なし）     決定して返す）
                                                        │
アプリ ←── host中間層 ←──────── 応答値 ←────────────────┘

進行役（dir） ────┐
                  ├─ 注入・sink（GPIO / Analog / UART RX / channel）
デバイス模型（dev）┘        │
                            ↓
                    EmbedBench核（同じ受付点で記録してから反映）
                            ├─ setPinValue → edge判定 → triggerInterrupt（ISR）
                            └─ pushRx / setAnalogValue など
```

## 最小I2Cシーケンスの手書き例（形式は未確定、計画5.2の候補1で仮置き）

`tests/device_route/` の案3シナリオをそのまま並べた例。仕込み（温度raw=250）は
走行前なので行が無く、走行中の注入は必ず行になる。

```text
00000001 0000000000000000 main app  i2c.write addr=48 len=2 data=0105 status=0
00000002 0000000000001000 tick dir  chan.write dev=temp0 chan=temp_raw value=300
00000003 0000000000002000 main app  i2c.write addr=48 len=1 data=00 status=0
00000004 0000000000002000 main app  i2c.read  addr=48 req=2 len=2 data=012C
```

## GPIO

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `pinMode` | app | core所有の `setPinModeHook` | 応答不要（host coreがmode保持） | — | hostが捨てた呼び出しはhookに届かず観測不能（X19）。扱いは未決2 |
| `digitalWrite` | app | core所有の `setPinWriteHook` | 応答不要 | — | listener上限到達はdiag（X9） |
| `digitalRead` | app | core所有の `setPinReadHook`（結果を含むイベントの完成タイミングは未決1） | pinにbindされた単一dev、未bindはheld値 | — | 同一pinへの二重bindは拒否+diag（X11と同型） |
| 線レベルの注入（IRQ線・DRDY等のdev出力を含む） | dir / dev | coreのGPIO sink（記録してから `setPinValue`） | — | core経由のみ | 走行外の注入は仕込みとして扱い記録しない（計画4.2） |
| `attachInterrupt` / `attachInterruptArg` / `detachInterrupt` | app | core所有の `setInterruptHook`（attach/detachイベント、1.7.1） | 応答不要（host coreが登録を保持） | — | 再attachはdetachなしの置換1イベント（X13） |
| ISR起動 | core | 同hookの `kInterruptEnter` / `kInterruptExit`（ISR内バス通信の文脈識別に使う、X16） | coreがedge判定し、sink記録後に `triggerInterrupt(pin)` で登録ISRを同期呼出し（X13/X16） | GPIO sinkと同経路 | 未登録pinのedgeはイベントのみ。ネスト規則はcore側の候補。**照合は正規化`InterruptTrigger`のみ**（生modeはesp32と不一致、X13） |

## Analog

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `analogRead` | app | core所有の `setAnalogReadHook`（結果の完成タイミングは未決1） | pinにbindされた単一dev、未bindはheld値 | dir / devがcoreのAnalog sink経由で `setAnalogValue`（記録） | 二重bind拒否+diag |
| `analogReadMilliVolts` | app | core所有の `setAnalogMilliVoltsHook`（1.7.1、完成タイミングは未決1） | pinにbindされた単一dev、未bindはheld値（X15） | dir / devがcoreのAnalog sink経由で `setAnalogMilliVolts`（記録） | raw hookと独立。二重bind拒否+diag |
| `analogReadResolution` / `analogSetWidth` | app | core所有の `setAnalogReadConfigHook`（1.7.1、両綴りは区別されない） | 応答不要 | — | — |
| `analogWrite` / `ledc*` / `dacWrite` / `tone` | app | core所有の `setAnalogWriteHook`（`AnalogWriteEvent` + `AnalogOut`） | 応答不要 | — | siliconが拒否する呼び出しはhost coreが無イベントで捨て、hookからは観測不能（X19実測: 拒否3種で0イベント）。扱いは未決2 |

## SPI

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `begin` / `end` / 設定変更 | app | core所有の `SPI.setLifecycleHook` | 応答不要 | — | — |
| `beginTransaction` / `endTransaction` | app | core所有の `SPI.setTransactionHook` | 応答不要 | — | — |
| `transfer`（1 byte） | app | core所有の `SPI.setTransferHook`（応答byteの完成タイミングは未決1） | busにbindされた単一dev（戻りbyte）。未bindは**hostの既定0xFF（アイドルバス）を維持**し、diagを付ける（0x00へ変える理由がないため既定に合わせる） | dev応答がそのまま注入 | transaction外のtransferはArduino上で必ずしも不正ではないため、エラーにせずイベントへ「transaction外」属性を残す。警告に格上げするかはGate D |

## I2C（Wire）

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `begin` / `setClock` など | app | core所有の `Wire.setLifecycleHook` | 応答不要 | — | — |
| write（`endTransmission`） | app | core所有の `Wire.setWriteHook`。イベントには**戻りstatusを含める**（完成タイミングは未決1） | アドレスにbindされた単一dev（status）。未bindはcoreが status 2 | — | 未bind write: status 2 + diag。二重bind拒否 + diag（X11）。**begin無しのendTransmission（status 4）と送信バッファoverflow（status 1）はhookに届かず観測不能**（X19）。扱いは未決2 |
| read（`requestFrom`） | app | core所有の `Wire.setReadHook`。イベントには**要求長と実応答長を含める**（完成タイミングは未決1） | 単一dev（bytes）。未bindは0 byte | — | 未bind read: 0 byte + diag（X11） |

## UART（デバイス向けport、console Serialは対象外）

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| TX（`Serial1.write` など） | app | core所有の `setActivityHook` の `kUartTx`（`write()` が戻る前、受理byte列のみ、1.7.1） | portにbindされた単一devが応答する。**応答はcoreのUART RX sink経由**（sinkが注入イベントを記録してから `pushRx`）。`kUartTx` callback内からsinkを呼べば即時性は保たれる（X14/X17の直接 `pushRx` は実験上の簡略で、候補coreでは迂回禁止。直接だと「devがいつ送信したか」が復元できない） | — | TX overflowで受理されなかったbyteはhookに届かず、`txOverflowed()` はsticky flagのため発生時刻・件数をイベント順序へ戻せない（X19実測: 1,200byte中1,024byteのみ通知）。扱いは未決2。hookとpolling併用は同一byteを2回見るためcoreはhook一本化（X14） |
| RX注入 | dir / dev | coreのUART RX sink（記録してから `pushRx`）。`pushRx` 自体はhost hookに通知されない（線の向こう側）ため、記録はsinkの責務 | — | core経由のみ | queue満杯時の受理byte数をdiag（候補） |
| RX消費（`read` / `readBytes`） | app | 同hookの `kUartRx`（消費1byteごと、1.7.1） | held queue | — | `flush()` の未読破棄は `kUartRxDiscard` で欠落なく残る（X14） |
| `begin` / `end` / 設定変更 | app | 同hookの `kUartBegin` / `kUartEnd` / `kUartConfig`（1.7.1） | 応答不要 | — | `uartNum()` でport識別 |

## 時間

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| wait要求（`delay` / `delayMicroseconds` / `yield` / Streamタイムアウトの内部wait） | app | core所有の `setClockHooks` のwait受付。固定tick境界で分割（X4）。**イベントの意味は「hostが要求したwait」であり元のAPI名ではない**: clock hookはwait長しか受け取れず、`yield()` と `delayMicroseconds(0)`、`delay(3)` の3スライスとStream timeoutのスライスは区別できない（X1）。API名まで必要になればhost拡張を依頼する | coreの仮想時計 | tick境界でdirの外部処理を実行（X8） | tick内の待ちAPIは「時間は進めtickは延期」+ diag が候補（X8）。凍結拒否は `delay()` を無限ループさせる。0 us waitの再入はguardで1重に制限し、連続回数の上限 + diag（X7、候補） |
| `millis` / `micros` | app | **記録しない（確定）**。イベントへのtimestamp付与自体が時計読取りであり、全読取りを記録すると再帰する | coreの仮想時計 | — | — |

## lifecycle

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `setup` / `loop` 前後 | host core | core所有の `setLifecycleHook`（main前に1回だけ登録、X5） | 応答不要 | — | 観測区間の候補: 開始 = `kPreSetup`、終了 = テストランナーが指定したloop回数（または終了条件）を満たした最後の `kPostLoop`。**最終loopの判断は核が持ち**、アプリは関与しない（未決3） |

## 診断そのもの

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| attach / detach / bind | dir | coreの受付（記録） | core（成功・拒否を戻り値で返す） | — | 上限到達・二重bindは戻り値 + diagイベントの両方（X9, X11） |
| イベントバッファ満杯 | core | 満杯方式によらず欠落カウンタ + 保持seq範囲で検出可能（X10） | — | — | 欠落を黙って成功にしない（X10） |

## 未決（Gate Aで承認が必要）

1. **イベント完成のタイミング**（応答を伴う操作: GPIO/Analog read、SPI transfer、
   I2C read/write）。候補は3つ:
   - (a) 要求イベントと応答イベントを2行に分ける
   - (b) 受付時にsequence/timestampだけ予約し、応答後にイベントを完成させる
   - (c) 応答callback中のイベント生成を禁止し、応答完了後に完成イベントを記録する
   応答callback（dev）がsinkを呼んで再入イベントを生む場合の順序が3案で変わるため、
   比較実験で数値を取ってから決める
2. **ログ完全性の範囲**。hostが受理せず捨てる呼び出しは現hookでは0イベント
   （X19実測: silicon拒否のanalog 3種、begin無しendTransmission、
   Wire送信バッファoverflow、UART TX overflowの超過分）。候補は2つ:
   - (a) 拒否・破棄も完全性の対象にし、hostへreject/overflow通知を追加依頼する
     （監査のH4依頼案。計画4.1「失敗もログ対象」とはこちらが整合する）
   - (b) 完全性を「hostが受理した外部作用」に限定し、拒否はアプリの戻り値と
     終了時の整合性診断（sticky flag等）だけにする
3. **実行区間**: 開始 = `kPreSetup`、終了 = テストランナー指定条件を満たした
   最後の `kPostLoop`、最終loopの判断は核が持つ——という候補の承認

解決済み:

- `millis` / `micros` は記録しない。timestamp付与自体が時計読取りであり、
  全読取りの記録は再帰する（旧・未決2）
- UART TXの暫定運用は1.7.1の `setActivityHook` で不要になった（X14）。
  TXはGPIOイベントと同じ順序軸に同期して残る
