# 実デバイスのデータと probe — ライブラリ開発とコア開発

文書状態: **検討中**（役割分担と流れの案。実機での実測はまだ無い）
文書基準日: 2026-10-29

この文書は、実バスを相手にする 2 つの用途——**ライブラリ開発**（相手はホストの模型。実デバイスのデータで
模型を作り・確かめる）と**コア開発**（相手は probe。Arduino コアの Wire / SPI を実バスで試す）——を、
関係するリポジトリの役割分担と合わせて整理する。OEP（Open Embedded Probe）側の
節は [oep] セッションの下書きを取り込んだもので、OEP の仕様・firmware の事実は
そちらが正本である。

関係するもの（パスは開発環境のもの。linkにはしない）:

- OEP: 仕様 `oep-spec`、probe の firmware `oep-probe-arduino`、host `oep-client-python`、
  記録と照合 `wireskein`
- `~/dev/I2CDeviceDB` — 実デバイスの振る舞いデータの暫定の置き場（作り直す前提。§5）
- 当時の harness 文書（ArduinoCore-CH32 の `harness-*`）— 2026-09-30 に役目を終えた（§10）

---

## 0. 結論（先に）

**用途は 2 つあり、確かめることが違う。濃淡はそこで付ける**（利用者）。

| | ライブラリ開発 | コア開発 |
| --- | --- | --- |
| 相手 | **ホストの模型**（EmbedBench の今の形） | **probe**（当座は ch32rv 用 Arduino コアの実機テスト） |
| 確かめること | ライブラリの論理。物理的な IF は見ない | コアの Wire / SPI の master の実装を、実バスで |
| 要るもの | **模型の振る舞いの正確さ** | **バスの条件を制御できること**と、**線を見て判定すること** |
| 実デバイスのデータ | ここで効く（収集 → 正本 → 雛形 → 照合） | ほとんど要らない。要るのは波形が正しいかの判定 |
| 持ち主 | **EmbedBench**（キャプチャ系） | **OEP 側**。デバイスの模型の再生はしない（OEP 側が決めた） |
| 節 | §2〜§7 | §8（参照） |

1. **ライブラリ開発: 主役はホスト相手のテストのまま。** EmbedBench の今の形を何も置き換えない。
   ただしホスト相手では模型の材料が取れない（相手が模型だから）。材料は実デバイスから取る:
   OEP の probe 1 台が既存のライブラリを動かして実デバイスを叩き、同じ線を自分でキャプチャし、
   ケースの目印を同じ時計に載せる。今の OEP の仕様のままでできる。
2. **EmbedBench の受け口は既にある。** `trace2regtable.py`（表と TODO）と `trace2tape.py`（録画の再生）が
   読む行に落とせば雛形になる。変換器 `skein2trace.py`（wireskein の復号 → 行）は作った（X66）。
3. **データの正本は EmbedBench の外、バスに依存しない新しいリポジトリに置く**（利用者が決定。
   I2CDeviceDB の履歴は引き継がない）。名前の候補と構成の案は §5。
4. **コア開発は OEP 側が持つ**（利用者が分担を決めた）。probe を相手にするのはコアの周辺の試験だけで、
   試験が先に決めた応答を前もって置く形（`oep-spec` の `docs/peripheral-testing.ja.md`）。**デバイスの模型の再生は OEP 側ではしない**
   （決めた中身を返す答える相手は OEP 側の機能の候補）。ライブラリの試験はホスト（EmbedBench）の領分（§8）。
5. **両方に共通**: 同じ P4、同じ wireskein の verify、同じ run の形（run.json）で回る。
   模型を使うのはライブラリ開発（ホスト）だけ。

---

## 1. 位置づけ

| 段 | 用途 | 相手 | 何が分かるか | 置き場 |
| --- | --- | --- | --- | --- |
| ホストのテスト | ライブラリ開発（主） | 模型 | ライブラリ・アプリが正しく振る舞うか。決定的で、CI で回る | EmbedBench |
| 収集 | ライブラリ開発 | 実デバイス | 実物が何を返し、いつ変わるか | OEP（入口）＋ データのリポジトリ（正本） |
| 照合 | ライブラリ開発 | 模型 vs 実データ | 模型が実物とずれていないか | EmbedBench（試験）＋ データのリポジトリ（材料） |
| 実機テスト | コア開発 | probe（試験が前もって置いた応答。標準に無い機能は OEP の peripheral-testing §3.2） | コアの master が実バスで正しいか | **OEP 側**（＋ wireskein の判定） |

**OEP の層（`oep-spec` の `docs/peripheral-testing.ja.md` §1。内側から 1 論理・2 DUT の中・3 観測・4 汎用の相手・
5 専用の相手・6 実物）との対応:** ホストのテストは**層 1**（EmbedBench の模型がスタブ）。収集は層 6 の実物を、層 4 の probe が
DUT として動かし、層 3 の観測（取り込みと wireskein）で記録するもので、**試験ではなく試験の前の準備**。
記録を層 1 のスタブ（模型）の中身にして層 6 を減らすのが、この文書の流れそのもの。照合（模型の準拠の試験）は、
「スタブはそれぞれの実装の試験で確かめる」に当たる。コア開発の実機テストは層 3・層 4 で、OEP 側が持つ（§8）。

ライブラリ開発の側の問題: 今の模型は datasheet と推測で書かれている。電源投入値、時間で落ちる status bit、
計測時間、NACK polling の長さは、実物で確かめていない（[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md)
X63 で「キャプチャはアプリが通った1本の道しか見せない」とした限界もそのまま残る）。§2〜§7 の流れは、
そこを実データで埋めるためのものである。

---

## 2. ライブラリ開発の流れ

```text
実デバイス
  │  ライブラリのケースを動かす（probe の上）
  ▼
収集  ── OEP: ストリーミングのキャプチャ（SDA/SCL・SPI・DRDY 等）＋ ProbeLog の目印（case/phase）
  │       （OEP が無いとき: CaptureWire 等のシム、sigrok + la2trace.py）
  ▼
正本  ── データのリポジトリ: wireskein の分析済み ＋ run.json ＋ ケースのソースと来歴（生のキャプチャは任意）
  │
  ▼  wireskein で復号 → 変換器 → EmbedBench の行（§6）
雛形  ── trace2tape.py（録画）／ trace2regtable.py（表 ＋ TODO）→ 人がフックを書く
  │
  ▼
ホストのテスト ── アプリ・ライブラリを模型相手に（今の EmbedBench）
  │
  ▼
照合  ── 同じケースを模型相手に回し、実データの行と比べる（§7）
  │       ずれ・TODO → データのリポジトリへ「次に実測する項目」として返す
```

**往復になるのが要点。** 雛形生成が決められなかった点（「時間だけで値が変わった」
「書いた値と読んだ値が違う」「電源投入値が不確か」）は、そのまま次の characterization
（ライブラリを使わずデバイスを直接叩くケース）の依頼になる。

---

## 3. 収集（OEP。ライブラリ開発）

この節と §4 は [oep] の下書きによる。「今のまま」は OEP の仕様を変えず、firmware の
サンプルと host 側だけでできることを指す。実測していないものは「要実測」と書く。

**構成は probe 1 台と実デバイスだけ。** `oep-probe-arduino` の `extras/LibraryTest` の形で、
ライブラリのケースを probe の firmware に入れる（`OepProbe.ino` の横に
`oepUserSetup` / `oepUserLoop` のファイルを置く）。ケースは別スレッド
（ESP32/P4 は FreeRTOS タスク、RP2 は core1）で動き、probe はその間も host に答え続ける。
host の pytest はキャプチャを始め、ケースを起こし（`run`、すぐ返る）、終わりを待ち
（`status`）、キャプチャを止める。P4X と RP2350 で確認済み。

**取り方はストリーミング。** OEP のロジックキャプチャは取りながら host へ流す
（wireskein の `capture --mode stream`）。窓の長さは probe のメモリで決まらず、
begin から何回もの測定まで 1 本に入る。流せる速さの上限は経路と probe で決まり、
標準の環境で実測してから決める（要実測）。I2C には足りる見込み
（100 kHz を 1 MHz、400 kHz を 4 MHz 以上、それぞれ数チャネル）。

**同じ時計に載るもの:**

- SDA / SCL と、DRDY・INT などの線（キャプチャのチャネルを足すだけ）
- ケースの目印（case / phase）: ProbeLog。RAM のリングに行を積むだけで線には何も出さず、
  各行に probe の時計（キャプチャの `start_ns` と同じ時計）が付く。
  `sample = (probe_ns − start_ns) × rate` でキャプチャ上の位置が出る（±数サンプル、確認済み）。
  UART マーカーの「送信完了を待ってから操作する」決まりも、`setup()` の通信が窓の外に
  なる問題も無くなる
- 転送の中身のシム（任意）: ライブラリが叩いた Wire の呼び出しを同じ時計で記録できる

| コア | Wire の入口 | SPI の入口 |
| --- | --- | --- |
| ESP32 Arduino 3.x（P4 を含む） | 仮想（`HardwareI2C`）。ライブラリ越しに捕まる | 仮想でない。ライブラリ越しには捕まらない |
| arduino-pico（RP2040 / RP2350） | 仮想（`HardwareI2C`） | 仮想（`HardwareSPI`） |

**正本はキャプチャ。** 線で何が起きたか（SCL の待ち、NACK のタイミング、controller の癖）は
キャプチャにしか無い。シムは「ライブラリが何を頼んだか」で、復号の照合と、キャプチャを
取らない軽い実行に使う。

**SCL の待ち。** 仕様の変更は要らない。キャプチャから host が測る（wireskein に足す。§6.3）。
線のレベルだけでは target が伸ばしたのか controller が間を空けたのかを区別できないので、
「stretch」ではなく **SCL の待ち**と呼ぶ。

**controller の違い。** 叩く側は probe のチップ（P4 なら ESP32 Arduino の Wire）に固定される。
来歴には controller とコアの版を残す（ESP32-S3 の SCL timeout で SHT30 の clock-stretch 読出しが
0 byte になった例が I2CDeviceDB の pilot にある）。

**OEP が無いときの入口**（既存。[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md) X64・X65）:

- `capture/` のシム（`CaptureWire` / `CaptureSPI` / `CaptureSerial` / `CaptureLines`）を
  任意のボードで使い、`micros()` 付きの行を直接出す
- 外部ロジックアナライザ（sigrok-cli）の復号を `la2trace.py` で行へ畳む（実測キャプチャで検証済み）

どちらも同じ行に落ちるので、§6 以降は入口に依らない。

## 4. SPI に広げたときの収集

- **サンプルレート。** I2C と同じく、高めのサンプルレートでストリーミングし、wireskein で復号する。
  時刻が残るので tape の `delayUs` も出る。ストリーミングで追いつかない速さの SPI は、
  収集のときにライブラリのクロックを落とすか（振る舞いが速さに依らない部品が多い。依る部品は
  来歴に残す）、短い区間をワンショットかリピートで速く取る。上限は標準の環境で実測してから決める（要実測）。
- **シムが効かない所。** ESP32 Arduino の `SPIClass` は仮想でないので、ライブラリ越しのシムはできない。
  表示器のライブラリ（LovyanGFX など）は `SPIClass` を通らずレジスタや DMA を直接使うので、
  どのコアでもシムには見えない。**SPI の正本はキャプチャ。**
- **量。** 表示器は 1 フレームで数百 KB になる。模型に要るのはコマンドの並びで、画素は要らないことが多い。
  D/C 線を wireskein の SPI のデータピンとしてもう 1 本指定すれば、バイトごとにコマンドかデータかが
  並んで出る（変更不要の見込み）。変換でコマンドだけを残し、画素は長さとハッシュにする。
  画面の中身の検査は [reference/tinygfx-emu/](reference/tinygfx-emu/README.ja.md) の絵での検査とつながる。
- **`spiTransfer(mosi) → miso` の形。** ホストの模型では問題にならない（同じバイトの MOSI を渡してから
  MISO を受け取れる）。実データとの照合では「MISO がそのバイトの MOSI より前の状態で決まっている」
  ことを確かめる材料になる。効いてくるのは probe の上で模型を再生するときだけで、それは OEP 側がしないと決めた（§8.1）。

---

## 5. データの置き場（ゼロベース）

I2CDeviceDB は暫定のもので、今の形（I2C の chip profile、jsonl の復号結果）では SPI・UART・線の
データを入れられない。置き場を最初から考え直す。**最終判断は利用者**で、ここは選択肢と推しを示す。

### 5.1 正本にするもの

- **正本は wireskein の分析済みデータ**（高めのサンプルレートで取ったものを復号した結果）。
  次を必ず一緒に持つ:
  - wireskein の版
  - ピンとプロトコルの指定（役割を決めた復号に使ったもの）
  - 未解釈の区間と、取りこぼした区間（lost / gap）。「何も起きなかった」と「見えなかった」を区別するため
  - run.json（ログ・目印・期待）、ケースのソース、来歴（chip、アドレス、バスの速さ、controller とコアの版、
    ライブラリと版、probe の id、配線）
- **生のキャプチャは任意で、原則は一緒に残す。** `.wireskein` は変化点の列なので、I2C なら長く取っても
  小さい（最初の試料は 8192 サンプルで約 1 KB）。重いもの（表示器の SPI など）だけ Git の外に置くか捨てる。
  生があれば、復号を直したときに作り直せる（2026-10-29 の `start_cond` / `scl_waits` の追加、
  X65 の sigrok の二重シフトのような誤り）。
- **バスに依存しない形**にする。I2C・SPI・UART・線を同じ run の中に持つ。
- **捨てない**。アドレス NACK の繰り返し（NACK polling）も、計測時間の手がかりなので残す。
  正規化（I2CDeviceDB の `retry-count-is-timing` のような落とし方）は受け取る側で行う。

### 5.2 置き場: 新しいリポジトリ（決定）

検討した 3 案と結果:

| 案 | 判断 | 理由 |
| --- | --- | --- |
| EmbedBench の中 | 採らない | EmbedBench は Arduino ライブラリで、配布物を丸ごと取る。キャプチャが全利用者に乗る。tests/ の「形式を固定する小さな試料」（§6.4）と役割が混ざる |
| I2CDeviceDB を作り直す | 採らない | 名前が I2C で、構造（I2C 前提の profile、jsonl）も壊すことになる。履歴は要らない（利用者） |
| **新しいリポジトリ** | **採る**（2026-10-29 利用者が決定） | 形に過去の制約が無い。I2CDeviceDB からは中身（chips / products / surveys / guides）だけを、履歴なしで移す |

### 5.3 名前: `EmbedBenchDeviceData`（決定）

**基準（利用者）:** 汎用のデータとして他にも使うなら関連の無い名前でよい。EmbedBench に特化するなら関連が分かる名前にする。

**判断: EmbedBench に特化している。** 使う側は EmbedBench の模型（ホストのテスト、照合）で、
何を集めるか・どのケースを書くかも模型の必要で決まる。
wireskein の識別の表を作る用途はあるが従である。形式はバスに依存しない一般的なものにしておき
（wireskein の分析済み ＋ run.json）、将来ほかに使うことは妨げない。

名前は利用者の流儀（`EmbedBench`・`I2CDeviceDB` の CamelCase）に合わせる。

| 候補 | 評価 |
| --- | --- |
| **`EmbedBenchDeviceData`**（**採用**。2026-10-29 利用者が決定） | 「EmbedBench のデバイス（模型）のためのデータ」とそのまま読める。`devices/`（模型の置き場）と言葉が揃う |
| `EmbedBenchRecords` | 短い。「実物の記録」は出るが、何の記録かは `DeviceData` より弱い |
| `EmbedBenchCaptures` | 分かりやすいが、正本はキャプチャでなく分析済みデータ（§5.1）なので名前と中身がずれる |

（関連の無い汎用名として `DeviceTraceDB` も検討したが、上の判断で外した。）
GitHub 上の名前の衝突は、作る前に確かめる（未確認）。

### 5.4 構成の案

```text
chips/      チップのカタログ（I2CDeviceDB から中身だけ移す。バス種別付き）
products/   製品の部品表（どのチップがどのバス・アドレスに載るか）
knowledge/  チップの知識の正本（レジスタ・コマンドの表。データまで、§5.5）
cases/      収集のケースのソース（LibraryTest 形式のライブラリのケース、characterization のケース）
runs/<chip>/<run-id>/
            run.json、wireskein の分析済み（版・指定・未解釈・lost / gap を含む）、
            来歴、キャプチャ（任意。重いものは Git の外）
guides/     人が読むデバイス解説
tools/      検証・生成（wireskein の識別の表、EmbedBench の雛形の初期値）
```

- run は「chip ごと」に置き、製品には依存させない（I2CDeviceDB の「キャプチャは chip 単位」は生かす）。
- EmbedBench はこのリポジトリを**読むだけ**（`skein2trace.py` → 雛形）。書き戻すのは TODO（次に実測する項目）で、
  それは issue か `knowledge/` の未決の印として返す。

### 5.5 チップの知識の置き場

チップの知識は今 3 か所にある: I2CDeviceDB の chip / profile、wireskein の `decl/devices`
（register_map / command_table。復号の識別用）、EmbedBench の regtable（キャプチャから推定した表）。

- **正本はデータのリポジトリに 1 つ**。wireskein の識別の表はそこから生成する。
- **EmbedBench の regtable は一方向の生成物ではない。** 「キャプチャから推定した表 ＋ 人が書いたフック」
  で、推定と TODO は正本側へ返す（§2 の往復）。正本のチップ情報は、雛形の初期値と照合に使う。
- **表に入れるのはデータまで**（[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md) X63 の結論）。
  「on write X after T us set Y」式の規則言語は作らない。CRC や FIFO を書いた時点で半端な言語になる。

---

## 6. EmbedBench が受け取る形

### 6.1 行（既存）

`trace2regtable.py` / `trace2tape.py` / `la2trace.py` が読み書きする転送単位の行。
先頭は µs の整数の時刻（probe の時計から換算した値でよい）。転送の時刻は START の時刻。

```text
<time_us> i2c.req addr=44 data=2400 stop=1 [rs]
<time_us> i2c.resp status=0|2|3            (Wire::endTransmission と同じ)
<time_us> i2c.rd.req addr=44 req=6 stop=1 [rs]
<time_us> i2c.rd.resp len=6 data=... [status=2]   (status=2: アドレスで断られた読出し)
<time_us> spi.xfer mosi=.. miso=..         (CS の窓 1 つ)
<time_us> uart.tx <text> / dev.tx <text>
<time_us> gpio.inject pin=N 0->1           (部品が駆動した線)
```

条件: **payload は省略しない**（EmbedBench 内部のトレースは 5 バイトで切れるので tape には使えない。X63）。
**アドレスで断られた読出しには `status=2` を付ける**（X67）。付けないと、アドレス NACK と「ACK したが 0 バイト」
（clock stretch の読出しが controller の timeout で切れたものなど）が同じ `len=0` になり、tape の待ちのステップ
（`--busy`）が誤って畳む。`la2trace.py`・`skein2trace.py` は付ける。ホストの環境の行は付けない（必要が無い）。

### 6.2 足す行と付記

| 追加 | 形 | 用途 |
| --- | --- | --- |
| 目印 | `<time_us> mark case=<id> [phase=<id>] [src=dut] [k=v...]` | ケース単位で tape を切る。TODO に「どの phase で変わったか」を付ける |
| SCL の待ち | 転送の行に `scl_wait=<us>`（その転送での合計） | 計測時間・clock stretch 読出しの材料。名前は「待ち」で、誰が伸ばしたかは言わない |
| 転送の終わり | 必要なら `end=<us>` | 転送の長さ（今は使わない。付けても壊れない） |

- 既存のツールは知らない行・知らない key を無視するので、どれも壊さずに足せる。
- **目印の値に注意。** 以前の `trace2regtable.py` は行のどこにあるイベント名でも拾い、
  `mark case=i2c.req` を転送として読んでいた。イベント名は「空白で区切られた語」に限るよう直した
  （本文書と同じ作業で修正。`tests/capture_scaffold/` に回帰試験）。それでも目印の値は
  `[A-Za-z0-9_-]` に限るのを約束にする。
- wireskein の run の目印は `#`（case）・`##`（phase）の深さで書かれ、`src=dut` の行（アプリの出力）も
  probe の時計を持つ。変換では `#` → `case`、`##` → `phase`、dut の行は `mark src=dut text=...` に落とす
  （`dev.note` は部品の note なので使わない）。

### 6.3 wireskein との対応と後処理

wireskein の analyze の JSON（[oep] 調べ）:

| 要るもの | wireskein | 状態 |
| --- | --- | --- |
| I2C の転送: 時刻・addr・rw・addr_ack・bytes・バイトごとの ack | transactions（s / e は tick） | あり |
| repeated start で始まったか、STOP で終わったか（`rs` / `stop=`） | 転送ごとの `start_cond`（S / Sr）・`end_cond`（P / Sr / null。null は窓の終わりで開いたまま） | wireskein e2ced47 で追加 |
| SCL の待ち | `scl_waits [{after_word, bit, ticks, s}]`、基準の `scl_low_ticks`（転送内の SCL low の中央値。その 4 倍を超えたものを出す） | wireskein e2ced47 で追加 |
| SPI の転送: CS の窓ごとの s / e、ピンごとの hex | transfers | あり。D/C はデータピンとして 1 本足す |
| case / phase の区画 | run の log の probe_ns（segments） | あり |
| 線（DRDY 等）の変化 | wireskein を Python から読む | あり |

**変換器は EmbedBench が持つ**: `devices/tools/skein2trace.py`（wireskein の I2C の転送 ＋ キャプチャの
`start_ns` / `tick_hz` ＋ run.json → §6.1 の行）。wireskein は EmbedBench を知らないままでよい。

- 時刻はすべて probe の時計の µs。転送は `start_ns + tick / tick_hz`、目印は run.json の `probe_ns`。
  `probe_ns` を持たない host 側の目印は、キャプチャの host 時刻 `t0` を介して置き、`clock=host` と書く。
- `status` は線から読んだ値（0 / 2 / 3）。**ライブラリの戻り値ではない**: 最初の試料は、線ではアドレス NACK（2）、
  Wire の `endTransmission` は 4（プルアップ無し）だった。
- 入力は今は「役割を決めた復号」の転送（wireskein の verify と同じ経路）。短い試料では `wireskein analyze`
  が I2C と主張せず claims が空になるため。役割が分かっている収集では、この経路が確か。
  analyze の JSON を直接読む形にするかは未決（§11）。
- SPI・線（DRDY）の変換はまだ無い。最初の SPI の実データが来たら同じ方針で足す。

### 6.4 形式は実データで固定する

`la2trace.py` の sigrok 対応は、合成データの試験が通っていたのに実データでアドレスを二重にシフトしていた
（X65）。変換器も同じ方針で、**wireskein の実物の出力を tests/ に小さく固定**してから対応を入れる。
最初の試料は P4X の LibraryTest（不在のアドレス 0x50 へ 2 バイト書き、プルアップ無しで
`endTransmission=4`、アドレス NACK）。小さく、NACK を含むので形式の固定に向く。
`tests/skein2trace/` に run.json・キャプチャ・転送を置き、出力の行を全部固定した（[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md) X66）。
長いケース（表示器もどき）と、読出し・repeated start・SCL の待ちを含む実データは次に実機が使えるときに取る。

---

## 7. ホストのテストでの使い方と照合

**手順は X64 のとおり、まず tape、次に表＋フック。**

1. **tape（録画の再生）。** 実データの run を `trace2tape.py` で tape にし（目印があれば `--case` / `--phase` で
   1 ケースずつ。測定待ちの NACK polling があれば `--busy`）、同じライブラリのケースを
   ホストで模型（`tape_model`）相手に回す。外れたら tape が名指しする
   （`tape 2: want write F425, got F426`）。ライブラリの版を上げたときの回帰試験にそのまま使える。
   ただし tape は「同じ道を通るか」しか言えない。
   `--busy` を付けないと、tape は polling を**回数**で再生し、速く poll するライブラリに実物より早くデータを返す
   （SHT30 の実データで、実物がまだ測定中の 11.2 ms に診断なしで返した。X67）。
2. **表＋フック。** `trace2regtable.py` で表と TODO を出し、人がフックを書く。分岐（別の設定、
   別の順序）でも答えられる模型になる。
3. **照合。** 同じケースを模型相手に回した行と、実データの行を比べる。
   - **2 層で判定する**: `(origin, text)` の列は完全一致、時刻は許容差
     （素の行の diff は時刻で全行が差分になる。[HARNESS_REQUESTS.ja.md](HARNESS_REQUESTS.ja.md) B-004 の指摘と同じ）。
   - 値が毎回変わる読出し（測定値）は、正本側で「意味を持たない値」と印を付けておき、比較から外す
     （I2CDeviceDB の `semantic_masks` の考え方は生かす）。
   - **ずれは模型の不具合か、実データの不足か**を分けて記録する。後者は次の実測の項目になる。

**時刻の精度。** 使うのは「コマンドから次の ACK 付き読出しまで」「値が時間だけで変わった間隔」
「tape の `delayUs`」で、10 µs 程度で足りる。実機の `micros()` の揺れが `delayUs` の照合にどう出るかは
まだ見ていない（X65「次に必要な実験」3）。

---

## 8. コア開発（OEP 側）

**分担（利用者、2026-10-29）: キャプチャ系（データ集め）は EmbedBench、コア開発の実機テストは OEP 側。**
OEP 側の記録は `oep-spec` の `docs/peripheral-testing.ja.md`（「周辺の試験の層と probe の機能」。ガイド）。要点:

- probe を相手にするのは**コアの周辺の試験だけ**。試験が先に決めた応答を probe に前もって置く。
- **デバイスの模型の再生は OEP 側ではしない。** ライブラリの試験はホスト側（EmbedBench）の領分。
- ただし、**決めた中身（FAT のイメージ、flash の中身など）を返す答える相手**（SD、SPI flash など）は、
  OEP 側の機能の候補。できるかどうかは原理ではなく、DUT が待つ時間のうちに probe が答えを用意できるかで決まる
  （その文書の §3.3）。振る舞いを決めるのは OEP 側の相手で、EmbedBench の模型ではない。
  中身のデータとして、EmbedBench の SD カードの模型が持つ FAT12 のプリセット 7 枚を流用する余地はある（要るとき OEP 側から）。
- 故障の注入（I2C の NACK、ストレッチの指定など。ROADMAP の C2）は、その文書の §3.2「標準に無い機能」に入っている。

事前調査（コアの Wire / SPI の今、標準の fixture でできる試験）は [REAL_DEVICE_ROADMAP.ja.md](REAL_DEVICE_ROADMAP.ja.md) §2 に残した。

### 8.1 模型を実バスに載せるとしたら（検討の記録）

OEP 側はデバイスの模型の再生をしないと決めたので、ここは使われない（答える相手は OEP 側が自分で持つ。§8）。方針が変わったときのために、模型のコードを読んで分かった事実だけ残す。

**凍結 IF のまま実バスに載るか。** 凍結 IF は転送をまとめて扱う
（`i2cRead` は長さと stop が分かっている前提、`i2cWrite` は全部受け取ってから status を返す）。
受け手の adapter が「書込みは終わってからまとめて渡す（各バイトは ACK）。読出しは SCL を待たせる間に最大長を頼み、
足りない分は 0xFF」とすると（模型のコードを読んで分類した。2026-10-29）:

| 区分 | 模型 | ずれる所 |
| --- | --- | --- |
| そのまま載る | temp / regmap / env_sensor / unit_encoder / unit_codec / unit_rtc / regtable（長さに依らないフック） | 異常の経路だけ: 書込みのデータ NACK が ACK に見える。regmap・env の「repeated start の無い読出しには答えない」が、失敗ではなく 0xFF の列に見える |
| 条件付き | tape（長さの照合を外せば） | 録画にある NACK は、バイト単位の拡張までは再現できない |
| 後で（拡張が要る） | unit_faulty | 故障（不在・拒否・途中で切れる読出し）はバイトごとの NACK か長さに依る。受け手のハードウェアがバイトごとの NACK を選べるなら、バイト単位の拡張を足せば書込みの拒否は再生できる見込み |
| 載らない | unit_imu | FIFO の取り出しが読む長さで決まる |

SPI: flash・sdcard・tape の `spiTransfer` は、**MISO を今のバイトの MOSI に依らず、前の状態だけで決めている**
（読んで確認した）。なので「次の MISO を先に聞く」形へ分けても振る舞いは変わらない。
ただしソフトウェアで次のクロックまでに答える必要があり、遅いクロックか、書くだけの部品（表示器）に限られる。

**要るときの拡張。** 凍結 IF の変更規則が認めるのは「既定の実装付きの仮想メソッドの追加」で
（[DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md) §3）、バイト単位の受け手（I2C）と「次の MISO」（SPI）は
revision 005 として足せる。既定の実装は今の転送単位のメソッドへ橋渡しし、既存の模型は無改造。
今は足さない（載せる予定が無い）。

---

## 9. 何が作れるか・何が解決するか

### ライブラリ開発

**作れるもの:**

- **実データ由来の模型。** 電源投入値・揮発 bit・計測時間・NACK polling の長さが実物どおりの模型を、
  ホストのテストでそのまま使う。
- **ライブラリの回帰試験（tape）。** 実デバイスでライブラリを 1 回動かした録画で、以後はハード無しで
  「同じ道を通るか」を CI で確かめる。ライブラリの版上げ・コアの版上げで道が変わったら名指しされる。
- **模型の照合試験。** 模型が実データから外れたら落ちる試験。模型の版
  （[HARNESS_REQUESTS.ja.md](HARNESS_REQUESTS.ja.md) B-006 の「観測可能な振る舞い」）を上げる判断の根拠になる。
- **多バスの振る舞いデータ（データのリポジトリ）。** 同じチップを複数のライブラリ・複数の controller で取って比べられる。

**解決する問題:**

- **模型が推測で書かれている**。datasheet に無い挙動（forced 測定後も CTRL_MEAS が読み返される、
  など）を実データで埋められる。
- **ハードが無いとライブラリやアプリを試せない**。録画と模型で CI に乗る。
- **実物で狙って起こせない状況**。正常系は実データ由来の模型で、故障は `unit_faulty_model` で、
  両方をホストで決定的に起こせる。
- **時間の扱いが根拠無しになりがち**。計測時間・SCL の待ちが実測値になり、圧縮した時間定数
  （B-001）と物理値の差も実データで確かめられる。
- **controller ごとの癖が埋もれる**。来歴に controller が残り、ESP32-S3 の timeout のような差を
  部品の故障と取り違えない。
- **収集の手間**。UART マーカーと外部ロジアナの同期、送信待ちの決まり、窓の長さの制約が、
  ProbeLog と ストリーミングで無くなる。

### コア開発（OEP 側。参考）

持ち主は OEP 側（§8、`oep-spec` の `docs/peripheral-testing.ja.md`）。EmbedBench から見た要点だけ:

- **コアの Wire / SPI の実機テスト**を、P4 の probe を相手に、同じ probe のキャプチャと wireskein の verify で判定して回せる。
  標準の fixture だけで始められる（試験の対応表は [REAL_DEVICE_ROADMAP.ja.md](REAL_DEVICE_ROADMAP.ja.md) §2.3）。
- 実物の部品では狙って起こせない NACK・stretch・timeout を、probe の設定で起こせる。

---

## 10. 過去の要望（H-185〜H-191）との関係

[HARNESS_REQUESTS.ja.md](HARNESS_REQUESTS.ja.md) の相手だった ArduinoCore-CH32 の
`harness-probe` / `harness-testing` / `harness-requirements` は 2026-09-30 に「役目を終えた文書」になり、
方針は OEP（`oep-workflow`）へ移った。**H-185〜H-191 は OEP へ引き継がれていない**
（oep-spec には EmbedBench への言及も H 番号も無い）。この文書で改めて整理する。

| 当時の要望 | この文書での扱い |
| --- | --- |
| B-001 圧縮した時間定数 | 収集とホストのテストには影響しない。実データで物理値との差を確かめる（§9） |
| B-002 requestWake の宣言 | 模型を probe で再生しない（§8）ので不要 |
| B-003 再入禁止 | 同上 |
| B-004 ログ形式と 2 層の diff | §6・§7 の行形式と照合の判定に置き換わる |
| B-005〜B-007、§4、§8 | 同上。閉じる |

---

## 11. 未決事項と要実測

**利用者の判断:**

1. ~~新しいリポジトリの名前~~ → `EmbedBenchDeviceData` に決定（§5.3）
2. チップの知識の正本の形（§5.5）。新しいリポジトリの設計と一緒に決める

**作業（担当）:**

| 作業 | 担当 |
| --- | --- |
| 転送の `start_cond` / `end_cond`、SCL の待ち | wireskein（e2ced47 で済み。push は利用者） |
| wireskein の I2C → 行の変換器、試料の固定 | EmbedBench（`skein2trace.py` で済み。X66） |
| 変換器の入力を analyze の JSON にするか（wireskein に「指示が役割を決めたら弱い証拠でも出す」口が要る） | 未決。wireskein と相談 |
| SPI・線の変換 | EmbedBench（実データが来てから） |
| コア開発の実機テスト、故障の注入 | OEP 側（§8。OEP の peripheral-testing） |
| `trace2tape.py` に case / phase の絞り込み | EmbedBench（済み。X67） |
| `la2trace.py` に GPIO チャネルの列（X65「次」4） | EmbedBench（OEP の線が来たら不要かもしれない） |
| `EmbedBenchDeviceData` の作成と構成（§5.4） | 利用者が作成 |

**要実測（実機が用意できたら）:**

- P4 のストリーミングで I2C / SPI を流し続けられる速さ
- U001-C（SHT30 + QMP6988）をライブラリで 1 ケース: 最初の 1 本。QMP6988（レジスタ型）で表＋フック、
  SHT30（コマンド型、NACK polling、SCL の待ち）で tape の往復
- 実機の `micros()` / probe の時計の揺れが tape の `delayUs` 照合にどう出るか
- コア開発の実測（P4 の I2C / SPI の受け手など）は OEP 側の記録（peripheral-testing）へ移した（§8）
