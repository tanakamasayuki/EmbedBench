# host-arduino-core拡張点の監査

内部の記録。日本語のみ。host-arduino-core 1.7.0だけでEmbedBenchの候補範囲を
実現できるかを、ソースと`tests/host_gaps/`の実測から整理する。

> **2026-09-03 追記: H1〜H3はhost-arduino-core 1.7.1で全て解決した。**
> 検証は `tests/interrupt_port/`、`tests/uart_activity/`、`tests/analog_mv/`
> （実験台帳のX13〜X15）。以下の不足表と依頼案は1.7.0時点の記録として残す。
> 1.7.1利用時の注意は末尾の「7. 1.7.1での解決内容と利用上の注意」を参照。

## 1. 結論

最初のI2C縦切り、仮想時計、ライフサイクル、GPIO/SPI/Wire/Analog rawの観測、
上位でのhook多重化は**追加依頼なしで開始できる**。

ただし、次を最終的な対象へ含めるならhost coreへの追加依頼が必要になる。

| 優先度 | 不足 | 影響 | 状態 |
| --- | --- | --- | --- |
| P0 | GPIO interruptの登録保持・callback呼出し口 | `attachInterrupt`がno-opのため、EmbedBenchがedgeを検出しても登録ISRを呼べない | **1.7.1で解決（X13）** |
| P0 | device UARTの同期activity hook | TXを後からqueue pollingするため、書込み時刻と他イベントとの順序を失う。waitしない即時応答もできない | **1.7.1で解決（X14）** |
| P1 | `analogReadMilliVolts`のread hook | mV読取りだけ観測・応答を差し替えできない | **1.7.1で解決（X15）** |
| P1 | UART begin/end/config/readの通知 | UART設定と受信消費を完全なイベント列へ載せられない | **1.7.1で解決（X14）** |

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

## 7. 1.7.1での解決内容と利用上の注意

依頼したH1〜H3は1.7.1で、要求した分担のまま提供された。実測はX13〜X15。

| 依頼 | 提供された口 | 検証結果 |
| --- | --- | --- |
| H1 | `HostInterrupt.h`: 登録保持（`attachInterruptArg`含む）、`triggerInterrupt(pin)`同期呼出し、`setInterruptHook`（attach/detach + handler前後のenter/exit） | 線の変化では発火しない。enter/exitでISR内バス通信を識別可能。handlerの自己detach安全。`fires`は再attachを跨いで保持（X13） |
| H2 | `HostUart::setActivityHook`: begin/end/config/TX/RX/RxDiscardの6イベント、TXは`write()`が戻る前、ロック解放後にcallback | TXがGPIOイベント間の正しい位置に残る。hook内`pushRx`で**wait 0回**の即時応答（1.7.0のpollingは1 wait/1,000us）。flush破棄も欠落せず報告（X14） |
| H3 | `setAnalogMilliVoltsHook`（held値を受け結果を返す）、`setAnalogReadConfigHook`（分解能変更） | raw hookと完全に独立。`clearAnalogHooks`は4本すべて解除（X15） |

EmbedBench側の利用規則（候補）:

1. **割り込みmodeの生値を照合に使わない。** このcoreの生定数はarduino-esp32と
   不一致（`RISING`=3/実機1、`CHANGE`=1/実機3）で、数値照合は静かに誤一致する。
   正規化された`InterruptTrigger`だけで照合する（X13で両方の生値を実測）
2. 再attachはdetachイベントなしのattachイベント1件（置換）。多重化層は
   置換として扱う
3. UARTはactivity hookとqueue pollingが共存し、両方使うと同一byteを2回見る。
   EmbedBenchはhook一本化とし、`readTx`は使わない
4. `kUartRx`は消費1byteごとに1通知。イベント量の見積りに含める
5. **devの外向き作用（GPIO・Analog・UART RX応答）はEmbedBenchのsink経由で
   記録してから反映する。** `pushRx` / `setPinValue` の直接呼び出しは注入イベントを
   残せない（`pushRx`はhost hookに通知されない）。X14/X17でhook内から直接
   `pushRx`したのは実験上の簡略であり、候補coreでは迂回を禁止する

## 8. H4 — 拒否・破棄されたAPI呼出しの通知（依頼案、Gate A判断待ち）

`tests/reject_paths/`（X19）の実測で、hostが受理せず捨てる呼び出しは
現hookでは一切イベントにならないことを確認した。

| 拒否・破棄 | 現状の見え方 |
| --- | --- |
| silicon拒否のanalog呼出し（未attachへのduty、周波数0、20bit超の分解能） | hook 0イベント。戻り値falseのみ |
| `beginTransmission`なしの`endTransmission` | status 4のみ。write hook 0回 |
| Wire送信バッファoverflow（128byte超） | status 1のみ。write hook 0回（模型へ届かない） |
| UART TX overflow | 受理された1,024byteだけ`kUartTx`通知。超過176byteは`txOverflowed()`のsticky flagのみで、発生時刻・件数をイベント順序へ戻せない |

Gate Aの未決2で「拒否も完全性の対象」を選ぶ場合に依頼する。要求する性質（案）:

- 受理経路と同じ同期タイミングで、拒否・破棄の理由と件数を1回で通知できる
- 既存hookのシグネチャを壊さない（別hookまたは共通の診断口）
- hostは拒否の判断自体を変えない（通知のみを追加する）

「hostが受理した外部作用」に完全性を限定する場合（未決2の(b)）は依頼しない。

## 9. H5 — CH32 のコアの API（L8。依頼案、保留）

2026-11-03。`REAL_DEVICE_ROADMAP` の L8（ArduinoCore-CH32RV の API を使うライブラリを host で動かす）のための洗い出し。
**依頼するかは保留**（利用者の判断待ち）。ここは、依頼するときにそのまま渡せる中身。

比べたもの: ArduinoCore-CH32RV（公開 API は同リポジトリの `docs/public-api.ja.md`、土台は ArduinoCore-API 1.5.2）と
host-arduino-core 1.7.1。部品のドライバが触る面（Wire・SPI・割込みの禁止・時間の換算・ポートのレジスタ・Servo）に絞った。
CH32 だけの物（`setRoute`、USB、DMA の ADC、flash、timer の貸し出し、`CH32RV` のシステムの物）は、部品のドライバが
使わないので外した。

「確認」の列: **ビルド** は host 1.7.1 で下の確かめのスケッチをビルドした結果、**読み** はソースを読んだだけ。

### 9.1 不具合（ArduinoCore-API の決まりと違う。追加とは分けて扱う）

| # | 現象 | 確認 | 直し方（案） |
| --- | --- | --- | --- |
| H5-B1 | `Wire.requestFrom(uint8_t addr, size_t len, bool stop)` が曖昧でビルドできない。`(int, size_t, bool)` も同じ。ArduinoCore-API の `HardwareI2C` の形そのもの | ビルド | `(uint16_t, size_t, bool)` と `(uint8_t, uint8_t, bool)` が、別々の引数で 1 回ずつ変換を要して並ぶ（`libraries/Wire/src/Wire.h:112-118`）。`size_t requestFrom(uint8_t, size_t, bool)` と `(int, size_t, bool)` を足す。足した後、2 引数の `(uint8_t, size_t)` が `(uint8_t, uint8_t)` に解決されて 255 を超える長さが切れないことも確かめる |
| H5-B3 | ライブラリのディレクトリ名に空白があると（`Adafruit SHT31 Library`、`Adafruit BusIO` をプロファイルで入れたとき）、`-I` が引用されず `g++: cannot specify '-o' with ... multiple files` でビルドが止まる | ビルド | `platform.txt` の include の並びを引用する（ほかの core と同じ `"-I{...}"` の形） |
| H5-B2 | `Wire.begin(uint8_t addr)`（スレーブとして始める）が、`begin(int sda = -1, int scl = -1, uint32_t f = 0)` に解決され、`sda = addr` のマスタとして始まる。エラーにならない | 読み（ビルドは通る） | `begin(uint8_t address)`（と arduino-esp32 と同じ `begin(int address)` の扱い）を足す。スレーブの模型が無いうちは、受け付けたことを診断で知らせるだけでもよい |

### 9.2 足りないもの（小さい。マクロ・空の関数・別名）

| # | 物 | 確認 | 中身（案） |
| --- | --- | --- | --- |
| H5-1 | `interrupts()` / `noInterrupts()` | ビルド | 標準の API。多くのライブラリが呼ぶ。host は割込みが 1 本の実行の中なので、空でよい（入れ子を数えて診断に出せればなおよい） |
| H5-2 | `SPI_HAS_TRANSACTION`、`WIRE_HAS_END`、`WIRE_HAS_TIMEOUT` | ビルド | ドライバが `#ifdef` で見る。`SPI.h` / `Wire.h` で定義する（`WIRE_HAS_TIMEOUT` は H5-3 と一緒に） |
| H5-3 | `Wire.setWireTimeout(uint32_t us = 25000, bool reset = false)`、`getWireTimeoutFlag()`、`clearWireTimeoutFlag()` | ビルド | AVR 由来の形。今の `setTimeOut(ms)` に寄せる。フラグは、模型が答えなかったときに立てる |
| H5-4 | `BUFFER_LENGTH`、`WIRE_INTERFACES_COUNT` | ビルド | `BUFFER_LENGTH` は `I2C_BUFFER_LENGTH` の別名（CH32 は 32、host は 128。値は host のままでよい） |
| H5-5 | `SPI.setPins(int sck, int miso, int mosi)`、`SPI_CLOCK_DIV2`〜`SPI_CLOCK_DIV128`、`SPI_INTERFACES_COUNT` | ビルド | `setPins` は `begin(sck, miso, mosi, -1)` と同じ扱い。`SPI_CLOCK_DIVn` は AVR の値 |
| H5-6 | `NOT_AN_INTERRUPT`、`F_CPU`、`clockCyclesPerMicrosecond()`、`clockCyclesToMicroseconds()`、`microsecondsToClockCycles()` | ビルド | DHT 型のドライバが使う。`F_CPU` は仮想の値（例: 48 MHz）に固定 |
| H5-7 | `IRAM_ATTR`（CH32 の `CH32RV_RAMFUNC` に当たる） | 読み | 空のマクロ |
| H5-8 | `OUTPUT_OPENDRAIN`（ArduinoCore-API の綴り。host は `OUTPUT_OPEN_DRAIN` だけ）、`attachInterruptParam` | 読み | 別名 |
| H5-9 | `<Servo.h>`（`attach`、`write`、`writeMicroseconds`、`read`、`attached`、`detach`） | ビルド | 動きの部品のライブラリが使う。PWM の書込みの hook（既にある）に載せる |

### 9.3 型の階層（大きい。ArduinoCore-API の型で書いたライブラリだけが要る）

| # | 物 | 確認 | 備考 |
| --- | --- | --- | --- |
| H5-10 | `arduino::HardwareI2C`、`arduino::HardwareSPI` の基底と `arduino::` 名前空間 | ビルド（`arduino` が無い） | `HardwareI2C&` / `HardwareSPI&` を受け取るライブラリ（SoftWire・SoftSPI もこれを継ぐ）がビルドできない |
| H5-11 | `PinStatus`、`BitOrder`、`SPIMode` などの enum | ビルド | **`BitOrder` は Adafruit BusIO（Adafruit の多くのドライバの土台）が要る。** BusIO は AVR・ESP32 などを名前で見分け、どれでもなければ `typedef BitOrder BusIOBitOrder;` とする。host は `ESP32` も `BitOrder` も無いので止まる。`SPISettings(f, MSBFIRST, SPI_MODE0)` 自体は通る |

### 9.4 模型が要るもの（依頼ではなく、EmbedBench の側の仕事と一緒に決める）

- ポートのレジスタ（`digitalPinToPort`、`digitalPinToBitMask`、`portOutputRegister`、`portInputRegister`。ビルドで無いことを確認）:
  OneWire・DHT のような速い bit-bang が使う。ポインタの先を、模擬したピンの状態につなぐ必要がある。
- Wire のスレーブ（H5-B2 の先）: 今は `onReceive` / `onRequest` を持つだけで、呼ぶ口が無い。

### 9.5 確かめ方

1 つのスケッチを、機能ごとの `-D` を変えてビルドし、通るかを見る。

```sh
arduino-cli compile --profile host --build-property "build.extra_flags=-DPROBE_X" .
```

試した呼び方（どれも `setup()` の中に置く。`#include <Wire.h>` と `<SPI.h>` は先頭に）:

| -D | 中身 | host 1.7.1 |
| --- | --- | --- |
| `PROBE_INTERRUPTS` | `noInterrupts(); interrupts();` | 宣言が無い |
| `PROBE_RF_SIZE` | `Wire.requestFrom(uint8_t(0x44), size_t(6), true);` | 曖昧 |
| `PROBE_RF_INT` | `Wire.requestFrom(0x44, size_t(6), true);` | 曖昧 |
| `PROBE_RF_U8` | `Wire.requestFrom(uint8_t(0x44), uint8_t(6), uint8_t(1));` | 通る |
| `PROBE_SLAVE` | `Wire.begin(uint8_t(0x08));` | 通る（マスタになる） |
| `PROBE_SPI_TX` / `PROBE_WIRE_HAS` | `#ifndef SPI_HAS_TRANSACTION` / `WIRE_HAS_TIMEOUT` で `#error` | 無い |
| `P_WIRE_TIMEOUT` | `Wire.setWireTimeout(25000, true); Wire.getWireTimeoutFlag(); Wire.clearWireTimeoutFlag();` | 無い |
| `P_BUFLEN` | `uint8_t b[BUFFER_LENGTH];` | 無い |
| `P_HW_I2C` | `arduino::HardwareI2C* h = &Wire;` | 無い |
| `P_SPI_PINS` / `P_SPI_DIV` | `SPI.setPins(1, 2, 3);` / `SPI.setClockDivider(SPI_CLOCK_DIV4);` | 無い |
| `P_SPI_SETTINGS` | `SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));` | 通る |
| `P_SERVO` | `#include <Servo.h>` | 無い |
| `P_CYCLES` | `microsecondsToClockCycles(1); clockCyclesPerMicrosecond();` | 無い |
| `P_PORTREG` | `portOutputRegister(digitalPinToPort(2)); digitalPinToBitMask(2);` | 無い |
| `P_NOTINT` | `NOT_AN_INTERRUPT` | 無い |

依頼して直ったら、同じ表がすべて「通る」になることと、`PROBE_SLAVE` がスレーブとして始まることを確かめる。

### 9.6 実際のドライバについて

DeviceData に run がある M5Unit-ENV 1.7.3 は、host でも CH32 でもこの面の外で止まる。依存の M5UnitUnified 0.6.4 が
`#if defined(ARDUINO)` で `esp32-hal-i2c.h`（`i2cIsInit`）と ESP32 の GPIO のレジスタを読む（ESP32 専用）。host は
`esp32-hal-i2c.h` が無くて止まる（ESP32 の API を名乗る host の core としては、これも不足の候補）。CH32 のドライバを
host で動かす確かめには、ESP32 に縛られないドライバを使う。

Adafruit SHT31 Library 2.2.2（＋ Adafruit BusIO 1.17.4）で確かめた（2026-11-03、利用者の了承のもと。DeviceData に run は無い）:

- host 1.7.1: H5-B3（空白のあるディレクトリ）を避けても、H5-11（`BitOrder`）で止まる。
- CH32 の core（作業ツリー 8f67590e、CH32V103）: `Adafruit_SHT31.h` の `extern TwoWire Wire;` が、core の
  `extern arduino::CH32RVTwoWire Wire;` とぶつかって止まる。この 1 行を除いた写しでは最後まで通る（flash 10,340 byte）。
  コアの不具合ではなく、型の違いでビルドできないライブラリ。CH32 の core は、`HardwareSerial` と同じく `TwoWire` /
  `SPIClass` を共通の型とし、`Wire` の型は具体的な `CH32RVTwoWire` にする（利用者の判断、[core] 2026-11-03）。
  SHT31 2.2.2 はこのままではビルドできず、同等のライブラリを使うか作る。
