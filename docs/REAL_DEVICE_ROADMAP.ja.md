# 実デバイスとの接続 — ロードマップと事前調査

文書状態: **計画**（2026-10-29 時点。実機での実測はまだ無い）
文書基準日: 2026-10-29

[REAL_DEVICE_DATA.ja.md](REAL_DEVICE_DATA.ja.md) が「何を・なぜ」を決めた設計記録で、この文書は「どの順で・何ができたら次へ」の
計画と、計画のために実機なしで行った事前調査をまとめる。進んだら各項目の状態を書き換える。

トラックは 3 本:

- **L（ライブラリ開発）**: 実デバイスのデータでホストの模型を作り・確かめる
- **C（コア開発）**: probe を相手に ch32rv 用 Arduino コアの Wire / SPI を実機で試す。**持ち主は OEP 側と ch32rv**
  （利用者の分担: キャプチャ系は EmbedBench、再生系は OEP 側）。ここには EmbedBench で行った事前調査だけを残す
- **D（データ）**: 新しいリポジトリ `EmbedBenchDeviceData`

---

## 0. 現状（済んだもの）

| 項目 | 内容 | 記録 |
| --- | --- | --- |
| 設計 | 2 つの用途、データの置き場、受け取る形、照合、再生の見通し | REAL_DEVICE_DATA.ja.md |
| 変換器 | wireskein の I2C の復号 ＋ run.json → 行（`skein2trace.py`） | X66 |
| 実データでの通し | I2CDeviceDB の pilot（SHT30・QMP6988）を雛形と tape に通した | X67 |
| tape の改良 | 待ちのステップ（`--busy`）、ケースの切り出し（`--case` / `--phase`） | X67 |
| 行の改良 | アドレスで断られた読出しに `status=2`、目印が転送に化けない照合 | X66・X67 |
| 決定 | 保存用は新規リポジトリ `EmbedBenchDeviceData`（履歴なし） | 利用者 |

---

## 1. トラック D: EmbedBenchDeviceData

（§1.1 の棚卸しは I2CDeviceDB の読み取り調査による。2026-10-29）

### 1.1 I2CDeviceDB の棚卸し

| ディレクトリ | 数 | 中身 | 移すか |
| --- | ---: | --- | --- |
| `chips/` | 83 | key、name、manufacturer、tags、addresses、datasheets、notes | **移す**。`addresses`（I2C だけ）をバスごとの `interfaces` に組み直す |
| `products/` | 194（すべて M5Stack） | key、vendor、sku、connector、`interfaces`、`components[{chip, addr}]` | **移す**。`components` を `{chip, bus, addr / cs / port}` に一般化。今は SPI が 0 件、UART・GPIO の部品は chip に結び付いていない |
| `libraries/` | 36 | key、repo、supports.chips | **移す**（そのまま） |
| `surveys/` | 1 | M5Stack の製品ラインの調べ方と対象外の SKU | **移す**（そのまま） |
| `guides/` | 83 ＋ 索引 | チップごとの日本語の解説 | **移す**。古い observation の id への参照は外す。索引は生成物なので移さない |
| `profiles/` | 76 | transport（`i2c-register` 59、`i2c-command` 10 ほか）、registers / commands、CRC、semantic_masks、evidence | **作り直す**。表（registers・commands・CRC の形）はデータとして生かし、`knowledge/` の形に移す。I2C の転送の項目で書かれた masks は形を変える |
| `scenarios/` | 76 | チップごとの characterization の操作の並び | **意図だけ移す**。実体は OEP の LibraryTest 形式のケースとして書き直す |
| `observations/`・`captures/` | 4・8 | SHT30・QMP6988 の pilot（sigrok） | **移さない**。wireskein で取り直す。SHT30 の行は EmbedBench の試料として残した（X67） |
| `collection/` | 510 | sigrok と UART マーカーの収集、probe のスケッチ 123 本 | **移さない**。OEP ＋ wireskein に置き換わる。スケッチの操作の並びは、ケースを書くときの参考 |
| `tools/` | 14 | validate、build、decode、curate、timing ほか | `validate`・`check_links`・`datasheet_text`・`guides_index`・`survey_m5stack` は**考え方を移植**。decode・curate・timing・probes は wireskein に置き換わる |
| `site/`・`docs/` | — | 公開サイト、設計文書 | **作り直す**。SPEC・DATA_MODEL の考え方（下）は引き継ぐ |

**引き継ぐ考え方**（I2CDeviceDB の SPEC・DATA_MODEL から）:

- **データは chip 単位で、製品に依存させない**。製品は部品表（どの chip がどこに載るか）。
- **カタログ（chip）を先に広く、振る舞い（knowledge）は後から深く**。後者はカタログの項目を繰り返さない。
- **内容から決まる id**: 復号した記録の sha256 の先頭 16 桁。時刻を除いて計算するので、同じ振る舞いは同じ id。
- **意味のハッシュ**（測定値などを masks で伏せた後のハッシュ）で、値だけが違う run をまとめる。
- **2 種類のケース**: ライブラリのケース（実際の使われ方）と characterization のケース（ライブラリを使わず直接叩く）。
- **逆引きの索引は保存せず、build で作る**。生成物は main に置かない。

**変えること:**

- 正本は sigrok の復号ではなく **wireskein の分析済み**（版・指定・未解釈・lost / gap を含む）。
- 時刻を捨てない。I2CDeviceDB は絶対時刻を保存しなかったが、EmbedBench の tape と照合には run の中の時刻が要る。
  （run の開始からの相対時刻でよい。日付は来歴に置く）
- **アドレス NACK の繰り返しを捨てない**（I2CDeviceDB は `retry-count-is-timing` として落としていた）。
  正規化は受け取る側で行う。

### 1.2 構成の案（REAL_DEVICE_DATA §5.4 を具体化）

```text
EmbedBenchDeviceData/
  chips/<chip>.yaml          カタログ（バスごとの interfaces）
  products/<product>.yaml    部品表（chip、bus、addr / cs / port）
  libraries/<library>.yaml
  surveys/
  knowledge/<chip>.yaml      振る舞いの表（レジスタ・コマンド・CRC・時間）。データまで。根拠の run を引く
  cases/<chip>/<case>/       収集のケースのソース（OEP の LibraryTest 形式）と、その意図
  runs/<chip>/<run-id>/
    run.json                 wireskein の run（ログ・目印・期待）
    analysis.json            wireskein の分析済み（版・指定・未解釈・lost / gap）
    provenance.yaml          chip、product、bus と速さ、controller とコアの版、ライブラリと版、probe、配線、日付
    capture.wireskein        生のキャプチャ（任意。重いものは Git の外）
  guides/
  tools/                     validate、索引の build、wireskein の識別の表と EmbedBench の雛形の初期値の生成
```

- **run-id** は I2CDeviceDB と同じく内容から決める（分析済みの転送の列のハッシュ。時刻を除く）。
  ファイル名は `<case>__<bus>__<speed>__<hash>` の形を引き継ぐ。
- **EmbedBench との境界**: EmbedBench は `runs/` を読むだけ（`skein2trace.py` → 雛形・tape）。書き戻すのは
  雛形生成の TODO で、`knowledge/` の未決の印か issue にする。

### 1.3 D のマイルストーン

| # | 内容 | 実機 | 完了条件 |
| --- | --- | --- | --- |
| D1 | リポジトリを作る（利用者）。上の構成で空の骨組み、README、`schema_version` | 不要 | 作成済み |
| D2 | chips・products・libraries・surveys・guides を移す。`addresses` と `components` を組み直す | 不要 | validate が通る |
| D3 | run の manifest（`provenance.yaml`）の形と validate | 不要 | EmbedBench の試料（libtest_run1）を 1 本入れて validate が通る |
| D4 | 最初の実データの run（L3 と同時） | 要 | U001-C の run が入り、EmbedBench から読める |
| D5 | `knowledge/` と、そこからの生成（wireskein の識別の表、EmbedBench の雛形の初期値） | 不要 | QMP6988 の knowledge から雛形の初期値が出る |

---

## 2. トラック C: ch32rv 用 Arduino コアの実機テスト（OEP 側・ch32rv が持つ。事前調査の記録）

（§2.1〜§2.3 は `ArduinoCore-CH32RV`・`oep-spec`・`oep-probe-arduino` の読み取り調査による。2026-10-29。
コアの持ち主は ch32rv のリポジトリで、ここは EmbedBench から見た計画である）

### 2.1 コアの Wire / SPI の今

| 項目 | 今の実装 | 出典 |
| --- | --- | --- |
| Wire の repeated start（書込み） | `endTransmission(false)` は STOP を出さず、次の START が repeated start になる | `libraries/Wire/src/Wire.cpp:545` |
| **Wire の repeated start（読出し）** | **`requestFrom(addr, n, false)` でも STOP を出す。** 長さ 1・2・3 以上のどの分岐も `stop()` を呼び、`!stopBit` の分岐は中身が空 | `Wire.cpp:552-644`（2026-10-29 に読んで確認。**コアのバグ候補、未報告**） |
| clock stretch と timeout | ハードウェアが待つ。フラグごとの待ちに `_timeout_us`（既定 25 ms）。`setWireTimeout` / `getWireTimeoutFlag` / `clearWireTimeoutFlag` あり。0 で無効 | `Wire.h:51-53`、`Wire.cpp:214-255` |
| `endTransmission` の戻り値 | 0 成功、1 バッファ超過（32 byte）、2 アドレス NACK、3 データ NACK、4 未開始・slave、5 timeout・バス異常・調停負け | `Wire.cpp:491-542` |
| バスの回復 | NACK 以外の失敗で次の呼び出し時に周辺を SWRST（自動）。`clearBus()`（SCL を最大 9 回、STOP）は明示呼び出しだけ | `Wire.cpp:454-463`、`854-904` |
| `setClock` | 100 kHz 以下は標準、超えると fast（duty 2:1）。上限の検査は無い。Fm+ は無い | `Wire.cpp:414-452` |
| SPI | mode 0〜3、LSB first、`transfer16/32`、`beginTransaction`。クロックは 2〜256 の 2 のべき分周。CS は利用者の GPIO。**DMA は無い** | `libraries/SPI/src/SPI.cpp` |

**既存の実機テスト:** OEP の fixture を使う試験は GPIO だけ（`tests/instrumented/gpio/`）。I2C・SPI の実機テストはまだ無い。
コア側の計画（`tests/test-plan-review-2026-10-24.ja.md`）は P4 の OEP を主のベンチとし、I2C の相手に
「NACK・stretch・バスの詰まりを起こせること」を求めている。

### 2.2 OEP の標準の相手でできること（P4）

数値は 2026-10-29 の `oep-probe-arduino` の値。正は probe の describe（firmware が変われば変わる）。

| fixture | 実装されている機能（P4） | describe |
| --- | --- | --- |
| `i2c-target` | configure（7 ビットアドレス）、`read_rx`、`preload_tx`（順に使う。空・使い切りは 0xFF）、`status`、**`stretch`**（受けた各データバイトの ACK の前と、読出しのアドレスの後に SCL を保つ。書込みのアドレスの後は対象外） | max_length 128、max_clock_hz 1 MHz、内蔵プルアップ（約 45 kΩ、弱い）、queue_depth 4、max_stretch_us |
| `spi-target` | configure（mode 0〜3、ビットの順）、`arm`、`read_rx`（MOSI と実ビット数）、`status` | max_length 64、max_clock_hz 24 MHz、LSB first、queue_depth 4、modes 0x0F（oep-probe-arduino 2dfb0b4 で出すようになった。それまでは必須の modes を出していなかった） |

**どちらにも故障の注入は無い。** i2c-target は書込みのバイトをすべて ACK する（データ NACK の op が無い）。

### 2.3 試験の対応表

**今の fixture でできる:**

| 試験 | 相手の設定 | 期待 |
| --- | --- | --- |
| 不在のアドレス | 別アドレスで configure | `endTransmission` 2、`requestFrom` 0。次の正常な転送が回復待ちなしで通る |
| 書込み 1 / 32 / 33 バイト | 既定 | `read_rx` に届く。33 バイトは 1 を返し何も送らない |
| 読出し 1 / 2 / 3 / N バイト | `preload_tx` | コアの 1・2・3 以上の分岐をそれぞれ通る。slot を超えた分は 0xFF |
| 書いてから repeated start で読む | `preload_tx` | キャプチャに Sr が見える（`endTransmission(false)` ＋ `requestFrom`） |
| stretch が timeout より短い | `stretch` 数 ms | 成功。キャプチャに SCL の待ち（`scl_waits`） |
| stretch が timeout より長い | `stretch` 30 ms 以上 | 5 を返し timeout フラグが立つ。次の呼び出しで回復する。`setWireTimeout(0)` や大きな値で結果が変わる |
| `setClock` 100 k / 400 k / それ以上 | 既定 | キャプチャで SCL の周波数と duty を測る。**400 kHz 以上は外付けのプルアップを付ける** |
| SPI mode 0〜3 × MSB / LSB × `transfer` / `16` / `32` | `arm` | `read_rx` で MOSI とビット数、arm した tx で MISO |
| SPI の分周 | 既定 | キャプチャで SCK を測る（24 MHz まで） |
| `beginTransaction` での切り替え | `arm` | 転送ごとに設定が変わる |

**足りないもの:**

| 試験 | 足りない物 | 案 |
| --- | --- | --- |
| データ NACK（戻り値 3） | i2c-target に NACK の op が無い | OEP 側で形を決める（§2.4）。P4 はバイトごとの ACK 制御をハードウェアが持つ |
| バスの詰まり（SDA / SCL が low のまま）、`recover()`・`clearBus()` | 線を low に保つ相手 | OEP 側で形を決める（§2.4）。GPIO fixture の open-drain low を同じピンに当てる手もあるが、plan の切り替えが要る |
| レジスタポインタに応じた応答 | `preload_tx` は固定の列 | 期待する読出しごとに host が slot を積む（短い試験なら足りる）。足りなければ OEP 側で（§2.4） |
| 調停負け・バス異常（戻り値 5、timeout 以外） | 起こす手段 | 未検討 |
| `requestFrom(..., false)` | 相手は足りている | **コアが常に STOP を出す**（上）。キャプチャで確かめれば、それがそのまま試験になる |
| 1 MHz を超える I2C | P4 の上限 | 対象外 |
| 64 バイトを超える SPI の転送 | spi-target の `read_rx` と `arm` の上限が 64 バイト | MOSI と SCK はキャプチャ ＋ verify で判定できる。MISO の内容と `read_rx` は 64 バイトまで |
| SPI の DMA | コアに無い | 対象外 |

### 2.4 その先（OEP 側）

C1（上の「今の fixture でできる」表を ch32rv の instrumented テストにする）は ch32rv と OEP で進める。
故障の注入と振る舞いのある相手（C2・C3）は、**OEP 側が形の案（標準の fixture に任意の op を足す / 種類ごとの汎用エミュレータ /
EmbedBench の模型の再生）を P4 の実験で比べてから決める**。検討と実験の記録は OEP 側に置く。
EmbedBench が関わるのは、そこで EmbedBench の模型を流用するときだけ（[REAL_DEVICE_DATA.ja.md](REAL_DEVICE_DATA.ja.md) §8.1）。

---

## 3. トラック L: ライブラリ開発（ホストの模型）

| # | 内容 | 実機 | 完了条件 |
| --- | --- | --- | --- |
| L1 | **QMP6988 の模型**: X67 の表（9 レジスタ）＋ フック（forced 測定で status が約 4.5 ms 立つ、normal mode で約 11 ms ごとに値が変わる、測定後も CTRL_MEAS を読み返す）。pilot のデータの tape と照合 | 不要 | 模型が tape と同じ値・同じ時刻を返す |
| L2 | **SHT30 の模型**（手書き。16 ビットのコマンド、CRC、NACK polling、clock stretch の読出し、測定時間は X67 の実測値） | 不要 | `tests/tape_replay/sht30_u001c.trace` の tape（`--busy`）と照合が合う |
| L3 | **照合の道具**: 実データの行と模型相手の行を比べる（`(origin, text)` は完全一致、時刻は許容差、測定値は伏せる） | 不要 | L1・L2 で、外れたときに名指しされる |
| L4 | OEP の P4 で U001-C を M5Unit-ENV ライブラリで収集 → `skein2trace.py` → L1・L2 の模型と照合 | 要 | 実データで照合が回る。外れは模型の不具合か実データの不足かに分けて記録 |
| L5 | SPI と線（DRDY）の変換（wireskein の SPI の転送・線の変化 → 行） | 要（最初の実データ） | 実物の出力を試料に固定 |
| L6 | 2 本目以降のチップ（BMP280、BH1750 など、I2CDeviceDB で guide のあるもの） | 要 | — |

**注意（L1・L2）:** pilot のデータは「統一前の手書き probe」で取った 1 本で、I2CDeviceDB 自身も恒久の正本ではないとしている。
L1・L2 の模型は L4 の実データで確かめ直す前提で作る。模型の版（HARNESS_REQUESTS B-006）は L4 で振る舞いが変わったら上げる。

---

## 4. 順序と依存

```text
実機なしで今できる:  L1 → L2 → L3          D1 → D2 → D3 → D5
                      │                      │
実機が要る:           L4 ←───────────────── D4（最初の run を入れる）
                      L5、L6

コア開発（OEP 側・ch32rv）: C1 → 故障の注入・再生（形は OEP 側が実験で決める）
```

- L・D（EmbedBench）と C（OEP 側）は独立に進む。共通なのは P4 の probe、wireskein の verify、run.json の形だけ。

---

## 5. 判断が要ること

| 項目 | 誰が | 選択肢 |
| --- | --- | --- |
| `requestFrom(..., false)` の STOP をコアの不具合として扱うか | 利用者（ch32rv の持ち主） | 直す / 仕様として文書化する |
| L1・L2 の模型を pilot のデータで先に作るか、L4 を待つか | 利用者 | **先に作るのを推す**（形と照合の道具が先に固まり、L4 は確かめ直すだけになる） |
| EmbedBenchDeviceData の `chips/` で I2CDeviceDB の key（部品番号の slug）をそのまま使うか | 利用者 | そのまま使うのを推す（guides の参照が保てる） |
