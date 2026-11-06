# 実デバイスのデータ — 模型を実物の記録で作り、確かめる

EmbedBench のテストの相手はホストの模型である。模型が正しいかは実物で決まるので、実デバイスを一度動かして線を記録し、
その記録から模型を作り、同じ記録で模型を確かめる。記録は試験ではなく、試験の前の準備である。

## 1. 役割

| 持ち主 | 持つもの |
| --- | --- |
| OEP | 収集。probe（P4X など）がケース（ライブラリを使うスケッチ）を動かし、同じ線を自分でキャプチャし、ケースの目印を同じ時計に載せる |
| wireskein | キャプチャの復号と run の形（`wireskein-run/1`）、部品の定義（`decl/devices`） |
| EmbedBenchDeviceData | 記録（`runs/`）、ケースのソース（`cases/`）、チップのカタログ（`chips/`）、振る舞いの表（`knowledge/`）。形は同リポジトリの `docs/DATA_MODEL.ja.md` |
| EmbedBench | 記録を行に落とす変換、模型、模型と記録の照合。`runs/` は読むだけ |

コア（Arduino の Wire / SPI の master）の実機テストは OEP 側が持つ。EmbedBench の模型を実バスに載せることはしない。

## 2. 流れ

```text
実デバイス ── OEP の probe がケースを動かし、キャプチャと目印を取る
  ▼
EmbedBenchDeviceData の run（run.json、キャプチャ、復号、provenance.yaml）
  ▼  devices/tools/skein2trace.py
EmbedBench の行（§3）
  ├─ trace2tape.py      録画の再生（tape_model）
  ├─ trace2regtable.py  レジスタの表と TODO → 人がフックを書く
  └─ tracecmp.py        模型と記録の照合
```

雛形生成が決められなかった点（時間だけで値が変わった、書いた値と読んだ値が違う、電源投入値が不確か）と、
照合で出たずれのうち記録が足りないものは、次に録るケースになる。

## 3. 行の形

`skein2trace.py` が書き、`trace2regtable.py`・`trace2tape.py`・`tracecmp.py` が読む、転送単位の行。
先頭は µs の整数の時刻（probe の時計）。転送の 2 行はどちらも START の時刻を持つ。

```text
<time_us> i2c.req addr=44 data=2400 stop=1 [rs]
<time_us> i2c.resp status=0|2|3
<time_us> i2c.rd.req addr=44 req=6 stop=1 [rs]
<time_us> i2c.rd.resp len=6 data=... [status=2]
<time_us> spi.xfer mosi=.. miso=..
<time_us> uart.tx <text> / dev.tx <text>
<time_us> gpio.inject pin=N 0->1
<time_us> mark case=<id> [phase=<id>] [end=1] [src=dut text=...] [clock=host]
<time_us> mark folded=<key> addr=<addr>
```

- **payload は省略しない。** EmbedBench の内部のトレースは 5 バイトで切れるので、tape の材料にはならない。
- `status` は線から読んだ値（0、2 アドレス NACK、3 データ NACK）で、ライブラリの戻り値ではない。
- **アドレスで断られた読出しは `status=2` を付ける。** 付けないと、アドレス NACK と「ACK したが 0 バイト」
  （stretch の読出しが controller の timeout で切れたもの）が同じ `len=0` になる。ホストの環境の行は付けない。
- `rs` は repeated start で同じアドレスに続いた転送、`stop=0` は repeated start で終わった転送。
- `scl_wait=<us>` は転送の中の長い SCL low の合計。線のレベルからは誰が SCL を下げていたか分からないので、stretch とは呼ばない。
- 目印: run の `#`（case）・`##`（phase）が `case` / `phase` になり、アプリの出力は `src=dut text=...`（パーセント符号化）。
  probe の時計を持たない host の目印は、キャプチャを始めた host の時刻との差で置き、`clock=host` を付ける。
  目印の値は `[A-Za-z0-9_-]` に限る。ツールは知らない行と key を無視する。
- `mark folded=` は、復号には出るが転送ではないもの（QMP6988 のリセットのバイト E6 がアドレス 0x73 に読まれる）。
  どれがそうかはチップの knowledge の `decode_artefacts` が決め、`skein2trace.py --provenance` が畳む。

変換器は、実物の出力を `tests/` に小さく固定してから直す（`tests/skein2trace/`）。合成データの試験だけでは、
実データの形の違いを見落とす。

## 4. 模型を作り、確かめる

1. **tape。** `trace2tape.py` で run を tape にし（`--case` / `--phase` で 1 ケースずつ、測定待ちの NACK polling は `--busy`）、
   同じケースをホストで `tape_model` 相手に回す。外れたら tape が名指しする。tape は「同じ道を通るか」しか言えない。
   `--busy` が無いと polling を回数で再生し、速く poll するアプリに実物より早く値を返す。
2. **表＋フック。** `trace2regtable.py` で表と TODO を出し、人がフックを書く。別の設定・別の順序にも答えられる模型になる。
   チップの knowledge があれば、`EmbedBenchDeviceData` の `tools/knowledge_table.py` が表を書き出す。
3. **照合。** `tracecmp.py` が記録の要求を記録の時刻どおりに模型へ渡し、答えを転送ごとに比べる。
   時刻そのものは比べない。遅い・早い模型は、違う答え（まだ立っている status bit、まだ変わらない値）として出る。
   測定値は世界のもので模型のものではないので、`--mask`（長さだけ）か `--changes`（変わったかだけ）で比べ、
   値は `--channel` で記録から与える。ずれは、模型の誤りか記録の不足かを分けて扱う。

模型の時間は記録が許す窓の中に置く。窓と値は knowledge の `timing` にあり、日によって数 % 動くものもある（QMP6988 の standby）。
controller の振る舞い（ESP32-P4 の Wire が stretch を約 6.7 ms で諦めるなど）は部品のものではないので、模型ではなく
照合の側（`tracecmp.py` の channel）で与える。

実物の記録で確かめた模型は `devices/src/qmp6988_model.*` と `devices/src/sht30_model.*`、照合は `tests/model_replay/`。
1 本の記録から作った模型は、別のライブラリ・別の controller の記録で確かめるまで信用しない。
凍結 IF の `i2cRead` は長さしか返せないので、clock stretch、途中まで答えた読出し、controller があきらめた後に
バスを空けるクロックで部品が値を出すことは、模型の答えとしては表せない（照合ではその差を説明の付く差として扱う）。

## 5. 残っている仕事

| 内容 | 持ち主 | 状態 |
| --- | --- | --- |
| SPI と線（DRDY）の変換（wireskein の SPI の転送・線の変化 → 行） | EmbedBench | 最初の実データを試料に固定してから |
| 2 本目以降のチップ（BMP280、BH1750 など） | EmbedBench・OEP | 収集から |
| どの模型にも被せられる故障（k 回アドレス NACK、n バイト目で NACK、k 回 busy の後に答える） | EmbedBench | 凍結 IF の `Device` を包む層にすれば IF を変えずに作れる。`unit_faulty_model` はその部分集合。言葉は OEP の `oep-spec` の `docs/peripheral-testing.ja.md` §3.2 にそろえる |
| 進行役が時刻を決めて出す線のパターン（level の列、繰り返し、出来事で始める） | EmbedBench | 模型への `lineIn` を時刻の列で与える形。言葉は同じく §3.2 にそろえる |
| CH32 の core（ArduinoCore-CH32RV）の API を使うライブラリを host で動かす | host の core | 足りないものは [HOST_CORE.ja.md](HOST_CORE.ja.md) §3。依頼は保留（利用者） |
| arduino-pico の API（PIO、マルチコア）を使うライブラリを host で動かす | host の core | どこまで要るかを、使う側のライブラリで決めてから。ベンチに pico が無い |
| CH32 の Wire の `requestFrom(…, false)` の修正（コア ba8dc851）を線で確かめる | OEP・コア | ベンチの X035C8T6 は I2C の errata の個体（ハードウェアの I2C でどの相手も ACK できない）。I2C の使える X035 か V シリーズをプローブにつなぐ配線を利用者が決める |
