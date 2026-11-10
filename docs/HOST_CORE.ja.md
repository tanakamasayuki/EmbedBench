# host core で使うときの決まりと、CH32 の API への対応

対象は lang-ship:host（host-arduino-core）1.8.0。EmbedBench の host 環境（`src/embedbench_host.*`）はこの上に載る。

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

## 3. CH32 の core の API を使うライブラリ

ArduinoCore-CH32RV の API を使う部品のドライバを host で動かすための面。比べたのは ArduinoCore-CH32RV（公開 API は
同リポジトリの `docs/public-api.ja.md`、土台は ArduinoCore-API 1.5.2）と host core で、部品のドライバが触る面
（Wire・SPI・割込みの禁止・時間の換算・ポートのレジスタ・Servo）に絞った。CH32 だけの物（`setRoute`、USB、DMA の ADC、
flash、timer の貸し出し、`CH32RV` のシステムの物）は、部品のドライバが使わないので外した。

### 3.1 host core 1.8.0 にあるもの

| # | 物 | host core の振る舞い（EmbedBench が知っておくこと） |
| --- | --- | --- |
| H-B1 | `Wire.requestFrom(uint8_t, size_t, bool)`・`(uint8_t, size_t)`・`(int, size_t, bool)`・`(uint8_t, uint8_t, uint8_t)` | どれも曖昧にならない。128 を超える長さは 128 に切る |
| H-B2 | 引数 1 つの `Wire.begin(addr)` | スレーブとして始まり、`Wire.slave()`・`Wire.slaveAddress()` が報告し、`[HostCore]` の注意を 1 回出す。**`onReceive` / `onRequest` は呼ばれない**（スレーブの模型は 3.2） |
| H-B3 | 空白のあるディレクトリのライブラリ（`Adafruit SHT31 Library` など） | `platform.txt` のレシピのパスがすべて引用され、ビルドできる |
| H-1 | `interrupts()` / `noInterrupts()` | 状態を記録するだけ（`HostArduino::interruptsEnabled()`）。**禁止中も `triggerInterrupt` は発火する**。禁止中の割込みをどう扱うかは呼ぶ側が決める |
| H-2 | `SPI_HAS_TRANSACTION`、`WIRE_HAS_END`、`WIRE_HAS_TIMEOUT` | 定義済み |
| H-3 | `Wire.setWireTimeout`、`getWireTimeoutFlag`、`clearWireTimeoutFlag` | フラグは、書込みの hook が status 5（AVR のタイムアウト）を返したときだけ立つ。host のバスは止まらないので core が自分で立てることはない |
| H-4 | `BUFFER_LENGTH`、`WIRE_INTERFACES_COUNT` | `BUFFER_LENGTH` は 128（host の値） |
| H-5 | `SPI.setPins(sck, miso, mosi[, ss])`、`SPI_CLOCK_DIVn`、`SPI_INTERFACES_COUNT` | `setPins` は次の `begin()` のピンを覚える |
| H-6 | `F_CPU`、`clockCyclesPerMicrosecond()` などの換算、`NOT_AN_INTERRUPT`、`getCpuFrequencyMhz()` | `F_CPU` は 240000000（host は arduino-esp32 を名乗るので ESP32 の値） |
| H-7 | `IRAM_ATTR`、`esp_attr.h`（`DRAM_ATTR`、`RTC_DATA_ATTR` など） | 空のマクロ |
| H-8 | `OUTPUT_OPENDRAIN`、`attachInterruptParam` | 別名 |
| H-9 | `<Servo.h>` | `ledcAttach(pin, 50, 16)`・`ledcWrite`・`ledcDetach` に載るので、どの段も analog の書込みの hook に出る |
| H-10 | `HardwareI2C`、`HardwareSPI`（`arduino::` からも見える） | `HardwareI2C` は arduino-esp32 3.x の形（bool を返す、`std::function` の callback）、`HardwareSPI` は ArduinoCore-API の形 |
| H-11 | `BitOrder`、`SPIMode`、`PinStatus`、`PinMode`、`pin_size_t` | `BitOrder`・`SPIMode` は enum で、マクロもそのまま。`PinStatus`・`PinMode`・`pin_size_t` は整数の typedef。`HIGH`・`LOW` はマクロ、`CHANGE`・`FALLING`・`RISING` は host の番号のまま（§1 の決まり 1） |

### 3.2 無いもの（模型が要る。EmbedBench の側の仕事と一緒に決める）

- ポートのレジスタ（`digitalPinToPort`、`digitalPinToBitMask`、`portOutputRegister`、`portInputRegister`）:
  OneWire・DHT のような速い bit-bang が使う。ポインタの先を、模擬したピンの状態につなぐ必要がある。
- Wire のスレーブの模型: `onReceive` / `onRequest` を呼ぶ口。

### 3.3 確かめ方

1 つのスケッチを、機能ごとの `-D` を変えてビルドし、通るかを見る（host core の側の `tests/runtime/api_compat` も同じ面を検査する）。

```sh
arduino-cli compile --profile host --build-property "build.extra_flags=-DPROBE_X" .
```

試した呼び方（どれも `setup()` の中に置く。`#include <Wire.h>` と `<SPI.h>` は先頭に）:

| -D | 中身 | host 1.8.0 |
| --- | --- | --- |
| `PROBE_INTERRUPTS` | `noInterrupts(); interrupts();` | 通る |
| `PROBE_RF_SIZE` | `Wire.requestFrom(uint8_t(0x44), size_t(6), true);` | 通る |
| `PROBE_RF_INT` | `Wire.requestFrom(0x44, size_t(6), true);` | 通る |
| `PROBE_RF_U8` | `Wire.requestFrom(uint8_t(0x44), uint8_t(6), uint8_t(1));` | 通る |
| `PROBE_SLAVE` | `Wire.begin(uint8_t(0x08));` | 通る。動かすと `slave()` が 1、`slaveAddress()` が 8 |
| `PROBE_SPI_TX` / `PROBE_WIRE_HAS` | `#ifndef SPI_HAS_TRANSACTION` / `WIRE_HAS_TIMEOUT` / `WIRE_HAS_END` で `#error` | 通る |
| `P_WIRE_TIMEOUT` | `Wire.setWireTimeout(25000, true); Wire.getWireTimeoutFlag(); Wire.clearWireTimeoutFlag();` | 通る |
| `P_BUFLEN` | `uint8_t b[BUFFER_LENGTH];` | 通る |
| `P_HW_I2C` | `arduino::HardwareI2C* h = &Wire;` | 通る |
| `P_SPI_PINS` / `P_SPI_DIV` | `SPI.setPins(1, 2, 3);` / `SPI.setClockDivider(SPI_CLOCK_DIV4);` | 通る |
| `P_SPI_SETTINGS` | `SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));` | 通る |
| `P_SERVO` | `Servo s; s.attach(5); s.write(90);` | 通る |
| `P_CYCLES` | `microsecondsToClockCycles(1); clockCyclesPerMicrosecond();` | 通る |
| `P_PORTREG` | `portOutputRegister(digitalPinToPort(2)); digitalPinToBitMask(2);` | 無い（3.2） |
| `P_NOTINT` | `NOT_AN_INTERRUPT` | 通る |

どれも警告は出ない。

### 3.4 実際のドライバについて

DeviceData に run がある M5Unit-ENV 1.7.3 は、host でも CH32 でもこの面の外で止まる。依存の M5UnitUnified 0.6.4 が
`#if defined(ARDUINO)` で `esp32-hal-i2c.h`（`i2cIsInit`）と ESP32 の GPIO のレジスタを読む（ESP32 専用）。host は
`esp32-hal-i2c.h` が無くて止まる（ESP32 の API を名乗る host の core としては、これも不足の候補）。CH32 のドライバを
host で動かす確かめには、ESP32 に縛られないドライバを使う。

Adafruit SHT31 Library 2.2.2（＋ Adafruit BusIO 1.17.4）で確かめた（DeviceData に run は無い）:

- host 1.8.0: プロファイルで入れて、`--warnings all` で警告なしにビルドできる。
- CH32 の core（CH32V103）: `Adafruit_SHT31.h` の `extern TwoWire Wire;` が、core の
  `extern arduino::CH32RVTwoWire Wire;` とぶつかって止まる。この 1 行を除いた写しでは最後まで通る（flash 10,340 byte）。
  コアの不具合ではなく、型の違いでビルドできないライブラリ。CH32 の core は、`HardwareSerial` と同じく `TwoWire` /
  `SPIClass` を共通の型とし、`Wire` の型は具体的な `CH32RVTwoWire` にする（利用者の判断）。
  SHT31 2.2.2 は CH32 ではこのままビルドできず、同等のライブラリを使うか作る。
