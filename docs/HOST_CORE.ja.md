# host core で使うときの決まりと、足りないもの

対象は lang-ship:host（host-arduino-core）1.7.1。EmbedBench の host 環境（`src/embedbench_host.*`）はこの上に載る。

## 1. 使う口と決まり

EmbedBench が使う host core の口と、確かめた振る舞い（`tests/interrupt_port/`・`tests/uart_activity/`・`tests/analog_mv/`）。

| 用途 | 口 | 振る舞い |
| --- | --- | --- |
| 割込み | `HostInterrupt.h`: 登録保持（`attachInterruptArg`含む）、`triggerInterrupt(pin)`同期呼出し、`setInterruptHook`（attach/detach + handler前後のenter/exit） | 線の変化では発火しない。enter/exitでISR内バス通信を識別可能。handlerの自己detach安全。`fires`は再attachを跨いで保持 |
| UART | `HostUart::setActivityHook`: begin/end/config/TX/RX/RxDiscardの6イベント、TXは`write()`が戻る前、ロック解放後にcallback | TXがGPIOイベント間の正しい位置に残る。hook内`pushRx`で**wait 0回**の即時応答。flush破棄も欠落せず報告 |
| Analog | `setAnalogMilliVoltsHook`（held値を受け結果を返す）、`setAnalogReadConfigHook`（分解能変更） | raw hookと完全に独立。`clearAnalogHooks`は4本すべて解除 |

EmbedBench の決まり:

1. **割り込みmodeの生値を照合に使わない。** このcoreの生定数はarduino-esp32と
   不一致（`RISING`=3/実機1、`CHANGE`=1/実機3）で、数値照合は静かに誤一致する。
   正規化された`InterruptTrigger`だけで照合する
2. 再attachはdetachイベントなしのattachイベント1件（置換）。多重化層は
   置換として扱う
3. UARTはactivity hookとqueue pollingが共存し、両方使うと同一byteを2回見る。
   EmbedBenchはhook一本化とし、`readTx`は使わない
4. `kUartRx`は消費1byteごとに1通知。イベント量の見積りに含める
5. **devの外向き作用（GPIO・Analog・UART RX応答）はEmbedBenchのsink経由で
   記録してから反映する。** `pushRx` / `setPinValue` の直接呼び出しは注入イベントを
   残せない（`pushRx`はhost hookに通知されない）ので、迂回しない。

## 2. 拒否・破棄された呼び出し

host が受理せず捨てる呼び出しは、hook ではイベントにならない（`tests/reject_paths/`）。記録が完全なのは「host が受理した外部作用」までで、拒否はアプリの戻り値と sticky flag でしか見えない。

| 拒否・破棄 | 見え方 |
| --- | --- |
| silicon拒否のanalog呼出し（未attachへのduty、周波数0、20bit超の分解能） | hook 0イベント。戻り値falseのみ |
| `beginTransmission`なしの`endTransmission` | status 4のみ。write hook 0回 |
| Wire送信バッファoverflow（128byte超） | status 1のみ。write hook 0回（模型へ届かない） |
| UART TX overflow | 受理された1,024byteだけ`kUartTx`通知。超過176byteは`txOverflowed()`のsticky flagのみで、発生時刻・件数をイベント順序へ戻せない |

## 3. CH32 の core の API を使うライブラリに足りないもの

ArduinoCore-CH32RV の API を使う部品のドライバを host で動かすときに、host core に足りないもの。
**この節（3.1〜3.3 と 3.5）は host core に依頼済み**で、host-arduino-core が取り込みを進める。取り込まれたら、ここを取り込んだ版に合わせて書き換え、3.5 の表の結果を更新する。

比べたもの: ArduinoCore-CH32RV（公開 API は同リポジトリの `docs/public-api.ja.md`、土台は ArduinoCore-API 1.5.2）と
host-arduino-core 1.7.1。部品のドライバが触る面（Wire・SPI・割込みの禁止・時間の換算・ポートのレジスタ・Servo）に絞った。
CH32 だけの物（`setRoute`、USB、DMA の ADC、flash、timer の貸し出し、`CH32RV` のシステムの物）は、部品のドライバが
使わないので外した。

「確認」の列: **ビルド** は host 1.7.1 で下の確かめのスケッチをビルドした結果、**読み** はソースを読んだだけ。

### 3.1 不具合（ArduinoCore-API の決まりと違う。追加とは分けて扱う）

| # | 現象 | 確認 | 直し方（案） |
| --- | --- | --- | --- |
| H-B1 | `Wire.requestFrom(uint8_t addr, size_t len, bool stop)` が曖昧でビルドできない。`(int, size_t, bool)` も同じ。ArduinoCore-API の `HardwareI2C` の形そのもの | ビルド | `(uint16_t, size_t, bool)` と `(uint8_t, uint8_t, bool)` が、別々の引数で 1 回ずつ変換を要して並ぶ（`libraries/Wire/src/Wire.h:112-118`）。`size_t requestFrom(uint8_t, size_t, bool)` と `(int, size_t, bool)` を足す。足した後、2 引数の `(uint8_t, size_t)` が `(uint8_t, uint8_t)` に解決されて 255 を超える長さが切れないことも確かめる |
| H-B3 | ライブラリのディレクトリ名に空白があると（`Adafruit SHT31 Library`、`Adafruit BusIO` をプロファイルで入れたとき）、`-I` が引用されず `g++: cannot specify '-o' with ... multiple files` でビルドが止まる | ビルド | `platform.txt` の include の並びを引用する（ほかの core と同じ `"-I{...}"` の形） |
| H-B2 | `Wire.begin(uint8_t addr)`（スレーブとして始める）が、`begin(int sda = -1, int scl = -1, uint32_t f = 0)` に解決され、`sda = addr` のマスタとして始まる。エラーにならない | 読み（ビルドは通る） | `begin(uint8_t address)`（と arduino-esp32 と同じ `begin(int address)` の扱い）を足す。スレーブの模型が無いうちは、受け付けたことを診断で知らせるだけでもよい |

### 3.2 足りないもの（小さい。マクロ・空の関数・別名）

| # | 物 | 確認 | 中身（案） |
| --- | --- | --- | --- |
| H-1 | `interrupts()` / `noInterrupts()` | ビルド | 標準の API。多くのライブラリが呼ぶ。host は割込みが 1 本の実行の中なので、空でよい（入れ子を数えて診断に出せればなおよい） |
| H-2 | `SPI_HAS_TRANSACTION`、`WIRE_HAS_END`、`WIRE_HAS_TIMEOUT` | ビルド | ドライバが `#ifdef` で見る。`SPI.h` / `Wire.h` で定義する（`WIRE_HAS_TIMEOUT` は H-3 と一緒に） |
| H-3 | `Wire.setWireTimeout(uint32_t us = 25000, bool reset = false)`、`getWireTimeoutFlag()`、`clearWireTimeoutFlag()` | ビルド | AVR 由来の形。今の `setTimeOut(ms)` に寄せる。フラグは、模型が答えなかったときに立てる |
| H-4 | `BUFFER_LENGTH`、`WIRE_INTERFACES_COUNT` | ビルド | `BUFFER_LENGTH` は `I2C_BUFFER_LENGTH` の別名（CH32 は 32、host は 128。値は host のままでよい） |
| H-5 | `SPI.setPins(int sck, int miso, int mosi)`、`SPI_CLOCK_DIV2`〜`SPI_CLOCK_DIV128`、`SPI_INTERFACES_COUNT` | ビルド | `setPins` は `begin(sck, miso, mosi, -1)` と同じ扱い。`SPI_CLOCK_DIVn` は AVR の値 |
| H-6 | `NOT_AN_INTERRUPT`、`F_CPU`、`clockCyclesPerMicrosecond()`、`clockCyclesToMicroseconds()`、`microsecondsToClockCycles()` | ビルド | DHT 型のドライバが使う。`F_CPU` は仮想の値（例: 48 MHz）に固定 |
| H-7 | `IRAM_ATTR`（CH32 の `CH32RV_RAMFUNC` に当たる） | 読み | 空のマクロ |
| H-8 | `OUTPUT_OPENDRAIN`（ArduinoCore-API の綴り。host は `OUTPUT_OPEN_DRAIN` だけ）、`attachInterruptParam` | 読み | 別名 |
| H-9 | `<Servo.h>`（`attach`、`write`、`writeMicroseconds`、`read`、`attached`、`detach`） | ビルド | 動きの部品のライブラリが使う。PWM の書込みの hook（既にある）に載せる |

### 3.3 型の階層（大きい。ArduinoCore-API の型で書いたライブラリだけが要る）

| # | 物 | 確認 | 備考 |
| --- | --- | --- | --- |
| H-10 | `arduino::HardwareI2C`、`arduino::HardwareSPI` の基底と `arduino::` 名前空間 | ビルド（`arduino` が無い） | `HardwareI2C&` / `HardwareSPI&` を受け取るライブラリ（SoftWire・SoftSPI もこれを継ぐ）がビルドできない |
| H-11 | `PinStatus`、`BitOrder`、`SPIMode` などの enum | ビルド | **`BitOrder` は Adafruit BusIO（Adafruit の多くのドライバの土台）が要る。** BusIO は AVR・ESP32 などを名前で見分け、どれでもなければ `typedef BitOrder BusIOBitOrder;` とする。host は `ESP32` も `BitOrder` も無いので止まる。`SPISettings(f, MSBFIRST, SPI_MODE0)` 自体は通る |

### 3.4 模型が要るもの（依頼ではなく、EmbedBench の側の仕事と一緒に決める）

- ポートのレジスタ（`digitalPinToPort`、`digitalPinToBitMask`、`portOutputRegister`、`portInputRegister`。ビルドで無いことを確認）:
  OneWire・DHT のような速い bit-bang が使う。ポインタの先を、模擬したピンの状態につなぐ必要がある。
- Wire のスレーブ（H-B2 の先）: 今は `onReceive` / `onRequest` を持つだけで、呼ぶ口が無い。

### 3.5 確かめ方

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

取り込まれたら、同じ表がすべて「通る」になることと、`PROBE_SLAVE` がスレーブとして始まることを確かめる。

### 3.6 実際のドライバについて

DeviceData に run がある M5Unit-ENV 1.7.3 は、host でも CH32 でもこの面の外で止まる。依存の M5UnitUnified 0.6.4 が
`#if defined(ARDUINO)` で `esp32-hal-i2c.h`（`i2cIsInit`）と ESP32 の GPIO のレジスタを読む（ESP32 専用）。host は
`esp32-hal-i2c.h` が無くて止まる（ESP32 の API を名乗る host の core としては、これも不足の候補）。CH32 のドライバを
host で動かす確かめには、ESP32 に縛られないドライバを使う。

Adafruit SHT31 Library 2.2.2（＋ Adafruit BusIO 1.17.4）で確かめた（DeviceData に run は無い）:

- host 1.7.1: H-B3（空白のあるディレクトリ）を避けても、H-11（`BitOrder`）で止まる。
- CH32 の core（CH32V103）: `Adafruit_SHT31.h` の `extern TwoWire Wire;` が、core の
  `extern arduino::CH32RVTwoWire Wire;` とぶつかって止まる。この 1 行を除いた写しでは最後まで通る（flash 10,340 byte）。
  コアの不具合ではなく、型の違いでビルドできないライブラリ。CH32 の core は、`HardwareSerial` と同じく `TwoWire` /
  `SPIClass` を共通の型とし、`Wire` の型は具体的な `CH32RVTwoWire` にする（利用者の判断）。
  SHT31 2.2.2 はこのままではビルドできず、同等のライブラリを使うか作る。
