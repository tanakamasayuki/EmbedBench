# 操作経路表（WP-A1）

内部の記録。日本語のみ。Gate Aの承認材料であり、承認前の**候補**である。
host-arduino-core 1.7.0のhook一覧と、[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md)の
X0〜X11の実測に基づいて、外部から観測可能な操作のすべてに記録点と応答担当を
割り当てる。「この操作はログに残らない」を暗黙に残さないための表であり、
記録点が存在しない行は不足として明示し、[HOST_EXTENSION_AUDIT.ja.md](HOST_EXTENSION_AUDIT.ja.md)の
依頼番号を付ける。

## アクターの凡例

| 記号 | アクター | 責務の候補 |
| --- | --- | --- |
| app | Arduinoアプリ | 無改造。Arduino APIだけを呼ぶ |
| core | EmbedBench核 | host coreの各単一hookを1枠ずつ所有し、受付時にsequence/timestampを付与して記録し、listenerとデバイスへ配送する（X5, X9） |
| dev | デバイス模型 | アドレス・pin・busにbindされ、応答値を一意に決める（X11） |
| dir | 進行役（テスト側） | 注入と時間の進行。すべての注入はcore経由 |
| diag | 診断 | 上限超過・未bind・満杯などをイベントとして残す（X9〜X11） |

## 経路の原則（候補）

1. host coreのhookはすべてcoreが1枠ずつ所有する。利用者・デバイスがhost coreへ
   直接登録する構成は成立しない（X5）
2. 記録点はcoreのイベント受付点の1か所。応答が必要な操作は、記録の後に
   単一の応答担当が値を決める（X11）
3. 走行中の外部状態変更はcore経由のみ。`setPinValue` などhost core APIの
   直接呼び出しは保証対象外とする（監査H1の分担と同じ）

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

進行役（テスト） ──注入・channel──→ EmbedBench核（同じ受付点で記録してから反映）
```

## 最小I2Cシーケンスの手書き例（形式は未確定、計画5.2の候補1で仮置き）

`tests/device_route/` の案3シナリオをそのまま並べた例。仕込み（温度raw=250）は
走行前なので行が無く、走行中の注入は必ず行になる。

```text
00000001 0000000000000000 main app  i2c.write addr=48 len=2 data=0105
00000002 0000000000001000 tick dir  chan.write dev=temp0 chan=temp_raw value=300
00000003 0000000000002000 main app  i2c.write addr=48 len=1 data=00
00000004 0000000000002000 main app  i2c.read  addr=48 len=2 data=012C
```

## GPIO

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `pinMode` | app | core所有の `setPinModeHook` | 応答不要（host coreがmode保持） | — | 範囲外pinはhost coreが黙って捨てるため、coreが範囲検査して診断イベント化（候補） |
| `digitalWrite` | app | core所有の `setPinWriteHook` | 応答不要 | — | listener上限到達はdiag（X9） |
| `digitalRead` | app | core所有の `setPinReadHook`（読取りと結果を1イベント） | pinにbindされた単一dev、未bindはheld値 | — | 同一pinへの二重bindは拒否+diag（X11と同型） |
| 線レベルの注入 | dir | coreの注入受付（記録してから `setPinValue`） | — | core経由のみ | 走行外の注入は仕込みとして扱い記録しない（計画4.2） |
| `attachInterrupt` / `detachInterrupt` | app | **なし（不足、H1）**。現状no-op | **なし（不足、H1）**。拡張後はcoreがedge判定し登録ISRを同期呼出し | 線注入と同経路 | 未登録pinのedgeはイベントのみ。ISRのネスト禁止はcore側の規則（候補） |

## Analog

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `analogRead` | app | core所有の `setAnalogReadHook` | pinにbindされた単一dev、未bindはheld値 | dirがcore経由で `setAnalogValue`（記録） | 二重bind拒否+diag |
| `analogReadMilliVolts` | app | **なし（不足、H3）** | held値（`setAnalogMilliVolts`）のみ。devを差し込めない | core経由held値注入 | 拡張までgolden対象から外す（候補） |
| `analogWrite` / `ledc*` / `dacWrite` / `tone` | app | core所有の `setAnalogWriteHook`（`AnalogWriteEvent` + `AnalogOut`） | 応答不要 | — | siliconが拒否する呼び出しはhost coreが無イベントで捨てるため、拒否も記録するならcore側で引数検査（候補） |

## SPI

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `begin` / `end` / 設定変更 | app | core所有の `SPI.setLifecycleHook` | 応答不要 | — | — |
| `beginTransaction` / `endTransaction` | app | core所有の `SPI.setTransactionHook` | 応答不要 | — | transaction外のtransferはdiag（候補） |
| `transfer`（1 byte） | app | core所有の `SPI.setTransferHook` | busにbindされた単一dev（戻りbyte） | dev応答がそのまま注入 | 未bindは既定0x00 + diag（候補） |

## I2C（Wire）

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `begin` / `setClock` など | app | core所有の `Wire.setLifecycleHook` | 応答不要 | — | — |
| write（`endTransmission`） | app | core所有の `Wire.setWriteHook` | アドレスにbindされた単一dev（status）。未bindはcoreが status 2 | — | 未bind write: status 2 + diag。二重bind拒否 + diag（X11） |
| read（`requestFrom`） | app | core所有の `Wire.setReadHook` | 単一dev（bytes）。未bindは0 byte | — | 未bind read: 0 byte + diag（X11） |

## UART（デバイス向けport、console Serialは対象外）

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| TX（`Serial1.write` など） | app | **なし（不足、H2）**。現状はclock wait中の `readTx` pollingで代替し、発生時刻と他イベントとの順序を失う（X3, X6） | portにbindされた単一devが `pushRx` で応答 | — | 拡張までTXの時刻はwait粒度でしか残らない |
| RX注入 | dir / dev | coreの注入受付（記録してから `pushRx`） | — | core経由のみ | queue満杯時の受理byte数をdiag（候補） |
| RX消費（`read` / `readBytes`） | app | **なし（不足、H2）** | held queue | — | — |
| `begin` / `end` / 設定変更 | app | **なし（不足、H2）** | 応答不要 | — | — |

## 時間

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `delay` / `delayMicroseconds` / Streamタイムアウト | app | core所有の `setClockHooks` のwait受付。固定tick境界で分割（X4） | coreの仮想時計 | tick境界でdirの外部処理を実行（X8） | tick内の待ちAPIは「時間は進めtickは延期」+ diag が候補（X8）。凍結拒否は `delay()` を無限ループさせる |
| `yield()` / 0 us wait | app | 同上（0 usとして受付） | coreの仮想時計（進まない） | 0 us waitが外部処理の唯一の機会（X7） | 再入はguardで1重に制限（X7）。連続0 us回数の上限 + diag（候補） |
| `millis` / `micros` | app | 記録しない（読取りのみ、候補） | coreの仮想時計 | — | — |

## lifecycle

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| `setup` / `loop` 前後 | host core | core所有の `setLifecycleHook`（main前に1回だけ登録、X5） | 応答不要 | — | 実行区間の開始・終了もここで確定（候補: preSetupからpostLoopまでを観測区間とする） |

## 診断そのもの

| 操作 | 呼ぶ側 | 記録点 | 応答担当 | 注入経路 | 失敗・診断 |
| --- | --- | --- | --- | --- | --- |
| attach / detach / bind | dir | coreの受付（記録） | core（成功・拒否を戻り値で返す） | — | 上限到達・二重bindは戻り値 + diagイベントの両方（X9, X11） |
| イベントバッファ満杯 | core | 満杯方式によらず欠落カウンタ + 保持seq範囲で検出可能（X10） | — | — | 欠落を黙って成功にしない（X10） |

## 未決（Gate Aで承認が必要）

1. 範囲外pin・silicon拒否操作など「host coreが黙って捨てる」呼び出しを、
   coreが引数検査して診断イベントにするか、対象外と明記するか
2. `millis` / `micros` の読取りを記録対象に含めるか（含めると量が爆発する）
3. 実行区間の定義（preSetup開始か、`setup`本体開始か）
4. UART TXの暫定運用（H2回答まで、wait粒度の順序で妥協するか、UARTを
   最初の縦切りから外すか）
