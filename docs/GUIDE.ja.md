# 入門ガイド

> English: [GUIDE.md](GUIDE.md)

EmbedBenchは、**組込みアプリケーションのロジックを、実機なしで、
ホスト上で検証する**ためのライブラリです。

このガイドは、これから触る人が最初に読むものです。
自分でデバイス模型を書く段になったら[上級ガイド](ADVANCED.ja.md)へ進んでください。

## 1. 何のためのものか

実機に繋いだ検証には、避けにくい弱点があります。

- **再現できない。** 「たまに失敗する」が調べられない
- **異常系を起こせない。** センサーを故意に壊すのは難しい
- **遅い。** 7.5 msの測定を100回待つと、それだけで時間が過ぎる
- **証拠が残らない。** 何が起きたのか後から辿れない

EmbedBenchは、この4つを引き受けます。時間は仮想なので `delay(2100)` は一瞬で終わり、
デバイスは模型なので好きなときに壊せ、何が起きたかは1行1イベントの記録に残ります。
同じ入力からは**必ず同じ記録**が出ます。

### やらないこと

**物理層は扱いません。** 波形、タイミングマージン、電気的特性、
ノイズ耐性——これらは実機でしか確かめられないので、最初から範囲外です。

扱うのは「アプリケーションが**何をしようとしたか**」です。
「0x76番地へ0xF4を書いて、7.5 ms待って、3バイト読んだ」までが対象で、
その信号が何ボルトで何ナノ秒だったかは対象外です。

## 2. 3つの登場人物

```text
  ┌─────────────┐        ┌───────────────┐        ┌──────────────┐
  │ アプリケーション │ ─────> │     環境       │ <────> │  デバイス模型  │
  │  (無改造の      │ <───── │ (記録・時間・   │        │ (温度計、GPS、 │
  │   Arduinoコード) │        │  バスの仲介)   │        │  故障する部品) │
  └─────────────┘        └───────────────┘        └──────────────┘
                                  │
                                  v
                            1行1イベントの記録
```

**アプリケーション**は、あなたが検証したいコードです。**書き換えません。**
`Wire.beginTransmission()` も `digitalRead()` も `delay()` も、
実機向けに書いたそのままが動きます。

**デバイス模型**は、繋がっている部品の振る舞いです。
`tests/common_models/` に22種の実例があります
（[カタログ](DEVICE_CATALOG.ja.md)参照）。

**環境**は両者を仲立ちし、起きたことを全部記録します。
実装例が2つあります——ホストのArduinoコア向け（`src/embedbench_host.*`）と、
純粋C++向け（`tests/common_env/nenv.*`）です。

> **重要:** 固まっているのは**デバイス模型と環境の境界**（`src/embedbench_device.h`）
> だけです。環境そのものは実装例であり、名前も構成もまだ動きます。

## 3. 最初のテストを読む

まず動かしてみます。

```sh
cd tests
uv sync
uv run pytest units_gpio -q -s
```

`tests/units_gpio/` は、ボタン・人感センサー・リレー・超音波距離計を
繋いだスケッチです。出力の中心はこれです。

```text
01 000000 main dir chan.write chan=0 data=01
02 000000 main dev gpio.inject pin=26 1->0 match=1
03 000000 isr  core isr.enter pin=26
04 000000 isr  app  gpio.write pin=2 val=1
05 000000 isr  core isr.exit pin=26
```

1行の読み方は左から順に:

| 列 | 意味 |
| --- | --- |
| `01` | 通し番号。飛んでいたら記録が欠けたということ |
| `000000` | 仮想時刻（マイクロ秒） |
| `main` | どの文脈か。`main` / `tick`（時間経過） / `isr`（割り込み中） |
| `dir` | 誰が起こしたか。`app` / `dev`（デバイス） / `dir`（テスト） / `core` / `diag` |
| 残り | 何が起きたか |

この5行は「テストがボタンを押した→デバイスが線を下げた→割り込みが走った→
アプリがLEDを点けた→割り込みが終わった」と読めます。
**因果が時刻と順序で残る**のがEmbedBenchの中心的な価値です。

## 4. 自分のテストを書く

実験1つ = 1ディレクトリです。

```text
tests/myexperiment/
  myexperiment.ino      ← スケッチ（検証対象＋配線）
  sketch.yaml           ← ビルド設定（既存からコピーでよい）
  test_myexperiment.py  ← 期待値
```

### スケッチの骨格

```cpp
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <temp_model.h>          // カタログの模型

static TempSensorModel sensor;

// 1) 模型が外界へ働きかけるための口。DevicePortは環境への配線を
//    済ませてあるので、必要なのは「この模型のこの線はどのピンか」だけ。
static ebhost::DevicePort port;

// 2) 世界のchannelを模型へ届ける
static bool onChannel(uint8_t ch, const uint8_t* d, size_t n, void*) {
  return ch == 0 && sensor.channelWrite(TempSensorModel::kChannelTemp, d, n);
}

// 3) バスと模型をつなぐ配線
static uint8_t onWrite(const uint8_t* d, size_t n, bool stop, bool cont, void*) {
  const ebdev::I2cTransfer xfer = {stop, cont};
  return sensor.i2cWrite(d, n, xfer);
}
static size_t onRead(uint8_t* d, size_t n, bool stop, bool cont, void*) {
  const ebdev::I2cTransfer xfer = {stop, cont};
  return sensor.i2cRead(d, n, xfer);
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start myexperiment");
  Wire.begin(21, 22, 400000);

  port.mapLine(TempSensorModel::kLineDataReady, 27);  // この模型の線はpin 27
  sensor.attach(&port);
  const ebhost::WireDeviceOps ops = {&onWrite, &onRead, nullptr};
  ebhost::bindWireDevice(0x48, ops);  // このアドレスはこの模型が担当
  ebhost::setChannelHandler(&onChannel);
  sensor.reset();

  ebhost::runBegin(1000);                // 記録開始。tickは1,000 us

  // 世界がセンサーに温度を与える。channelはこのためのもので、
  // バスでもピンでもなく「テストが現実を動かす」口である。
  const uint8_t reading[2] = {0x00, 0xFA};
  ebhost::chanWrite(ebhost::Origin::kDir, 0, reading, 2);

  // --- ここから下が検証したいコード。実機向けのまま ---
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(uint16_t(0x48), size_t(2), true);
  const int hi = Wire.read();
  const int lo = Wire.read();
  // ---------------------------------------------

  ebhost::runEnd();                      // 記録終了

  static char trace[2048];
  ebhost::formatTrace(trace, sizeof(trace));
  Serial.printf("values temp=%02X%02X\n", hi, lo);
  Serial.print(trace);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n",
                s.events, s.dropped, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
```

### 期待値の書き方

```python
def test_myexperiment(dut):
    dut.expect("TEST start myexperiment", timeout=10)
    dut.expect("values temp=00FA", timeout=10)
    dut.expect("01 000000 main dir chan.write chan=0 data=00FA", timeout=10)
    dut.expect("02 000000 main dev gpio.inject pin=27 0->1 match=0", timeout=10)
    dut.expect("06 000000 main dev i2c.rd.resp len=2 data=00FA re=5", timeout=10)
    dut.expect("stats events=6 dropped=0 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
```

行を固定するのは面倒に見えますが、**記録が変わったら必ず気づく**ための仕掛けです。
変わってよい変更なら期待値を更新し、そうでなければ退行を捕まえたことになります。

## 5. 何を検査すべきか

一番大事な指針です。

> **エラーが無いことではなく、値を検査してください。**

`tests/units_misuse/` に実例があります。registerを選ばずにI2C読み出しをすると、
バスは正常、部品も応答、statusも0、**しかし返ってくるのは別のregisterの値**です。

```text
08 000000 main dev i2c.rd.resp len=2 data=0100 re=7
```

「間違った質問をした」ことは、どんなstatus codeでも報告できません。
値そのものをassertしていれば捕まりますが、
「エラーが起きていないこと」だけを見ていると通り抜けます。

### 毎回見るべき3つの数字

```text
stats events=15 dropped=0 diag=6 outside=2 windows=1
```

| 数字 | 意味すること |
| --- | --- |
| `dropped` | **0でなければ記録が欠けている。** 期待値の意味が変わる |
| `diag` | 環境が付けた診断の数。想定外なら理由を確認する |
| `windows` | **0なら `runBegin` を忘れている。** 記録が空なのは当然 |

## 6. よくあるつまずき

**記録が空、`windows=0`。**
`ebhost::runBegin()` を呼んでいません。ホスト側のフックを入れるのが `runBegin` なので、
それより前のバス操作は環境から**見えてすらいません**。

**`delay(500)` が一瞬で終わる。**
仕様どおりです。時間は仮想なので、待っても実時間は消費されません。
なお `delay()` はミリ秒、`delayMicroseconds()` はマイクロ秒です。
イベントの時刻が想定の1000倍なら、単位を間違えています。

**`diag.unbound addr=XX` が出る。**
そのアドレスに模型を割り当てていません。`ebhost::bindWireDevice()` を確認してください。

**デバイスの応答が想定より遅い。**
模型が `requestWake()` を呼んでいない、または `HostPort` で
`requestWake` を実装し忘れています。これが無いと、応答はtick境界へ丸められます。

**期待値の `dut.expect` が一致しない。**
`*` や `.` や `+` は正規表現として解釈されます。`re.escape()` を使ってください。

## 7. 次に読むもの

- [上級ガイド](ADVANCED.ja.md): 自分でデバイス模型を書く
- [デバイスカタログ](DEVICE_CATALOG.ja.md): 24種の模型の一覧と選び方
- [tests/README.ja.md](../tests/README.ja.md): 62実験の索引
- [実験台帳](EXPERIMENTS.ja.md): なぜこの設計なのかの根拠（実測つき）
