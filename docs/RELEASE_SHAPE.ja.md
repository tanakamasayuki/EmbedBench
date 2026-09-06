# 公開形の検討（revision 100 へ向けて）

実験60回・模型22種・実験64件を書き終えた時点（2026-09-06、この検討の基準日）で、**利用者が実際に何を打つか**を
数えて、公開すべき形を再検討した。以下は実測に基づく提案であり、決定ではない。

## 0. 今どうなっているか

利用者が書くスケッチの先頭は必ずこうなる。

```cpp
#include <EmbedBench.h>          // 中身はversion定数のみ。実質空
#include <embedbench_host.h>    // 実際のAPIは全部こっち
```

`EmbedBench.h` にはこう書いてある——「この入口は意図的に検証APIを公開しない」。
`embedbench_host.h` にはこう書いてある——「実験候補であり承認されたAPIではない。
名前・フィールド・振る舞いは暫定で、手戻りを見込んでいる」。

**この2つを掲げたまま公開はできない。** ここが出発点。

## 1. 層の分け方は既に正しい

構造そのものは変える必要がない。実測で裏が取れている。

| 層 | 実体 | 可搬性 | 安定度 |
| --- | --- | --- | --- |
| デバイスIF | `embedbench_device.h` | **純粋C++11**。プラットホームヘッダ無し | **凍結済み**（v1 / rev 004） |
| 環境 | `embedbench_host.{h,cpp}` | Arduinoホストコア専用 | 暫定 |
| 環境#2 | `tests/common_env/nenv.*` | 純粋C++ | 参考実装 |
| 模型 | `devices/`（数は[FACTS](FACTS.ja.md)） | **純粋C++11** | 実例 |

模型がg++単体でビルドできることは `tests/device_if/` と `tests/contracts/` が
毎回検証している。**分離は実態として既に成立している**ので、
直すべきは提示の仕方だけである。

## 2. 実測: 利用者が実際に書いているもの

### 2.1 `HostPort` の定型文が28スケッチで412行

全64実験のうち28がHostPortの派生クラスを書いており、その総量は412行。
中身の内訳がはっきりしている。

| メソッド | 実装数 | うち中身が無いもの |
| --- | ---: | ---: |
| `serialOut` | 30 | **20**（`return true;` が10、`return false;` が10） |
| `lineOut` | 30 | **18**（`{}`） |
| `requestWake` | 12 | 0 |
| `diagnose` | 12 | 0 |
| `frameOut` | 9 | 0 |
| `analogOut` | 3 | 0 |

`serialOut` と `lineOut` だけが**純粋仮想**なので、シリアルを持たない部品でも
線を持たない部品でも、必ず空実装を書かされる。
しかも**空実装の戻り値が `true` と `false` に割れている**——
「使わないportは何を返すべきか」を誰も決められていない証拠である。

他の任意メソッド（`analogOut`・`requestWake`・`diagnose`・`frameOut`）は
既定実装つきで、書かなければ書かなくてよい。**2つだけが不揃いになっている。**

### 2.2 呼ばれているAPIは公開面の半分

`ebd::` の公開関数は40。スケッチが実際に呼んでいるのは上位に偏る。

```text
39 registerFormat   34 chanWrite    31 runBegin/runEnd/formatTrace
38 nowUs            30 stats        18 bindTickDevice
35 dumpf            19 WireDeviceOps 17 setChannelHandler/bindWireDevice
```

一度も呼ばれていない、あるいは実験の自己計測にしか使われていないもの:
`eventBytes`・`respLineCount`・`eventCount`・`pendingWakeUs`・
`listenerCapacity`・`deferralCapacity`・`frameCapacityBits`。
**7つは利用者向けAPIではなく実験用の内観**であり、同じ面に並べる理由がない。

### 2.3 `Origin` は88回書かれている

```text
37 Origin::kDir   36 Origin::kDev   15 Origin::kApp
```

注入系の関数は全部が第1引数に `Origin` を取る。
利用者側（テスト）はほぼ `kDir`、adapter側（port実装）はほぼ `kDev` で、
**呼ぶ場所で決まっている**。88回のうち大半は書く必要のない情報である。

## 3. 提案

### 提案1: 入口を1つにする

```cpp
#include <EmbedBench.h>   // これだけ
```

`EmbedBench.h` が凍結IFとホスト環境をまとめて取り込む。
`library.properties` の `includes=EmbedBench.h` とも整合する。

### 提案2: `draft` の名を落とす

`embedbench_host.{h,cpp}` → `embedbench_host.{h,cpp}`。
凍結IFの `embedbench_device.h` と対になり、
**「デバイス側」と「ホスト側」**という分け方が名前から読める。

### 提案3: 名前空間を `ebd::` から `ebhost::` へ

現在は `ebd::`（環境）と `ebdev::`（デバイスIF）が並んでいる。
**2文字違いで、しかも片方は凍結済みで変えられない。**
1つのスケッチに両方が出てくるので、読み違えが起きる形になっている。

`ebdev::`（device側・凍結）と `ebhost::`（host側）にすれば対称になる。
機械的な置換で済み、**外部利用者がまだ居ない今しか払えないコスト**である。

### 提案4: 既製のportを同梱する（最大の効果）

412行の定型文の大半は、線→ピンの対応表があれば消える。

```cpp
// 現在: 毎回これを書く
class MyPort : public ebdev::HostPort {
 public:
  uint64_t nowMicros() override { return ebd::nowUs(); }
  void lineOut(uint8_t, uint8_t level) override {
    ebd::pinInject(ebd::Origin::kDev, 27, level);
  }
  bool serialOut(const uint8_t*, size_t) override { return false; }
  bool requestWake(uint64_t w) override { return ebd::requestWake(w); }
  bool diagnose(const char* t) override { ebd::deviceNote(t); return true; }
};

// 提案: 対応だけ書く
ebhost::DevicePort port;
port.mapLine(TempSensorModel::kLineDataReady, 27);
```

`requestWake`・`diagnose`・`frameOut`・`formatId`・`nowMicros` は
環境へ流すだけなので既製で足りる。実装が要るのは**対応付けだけ**である。

なお `requestWake` の実装忘れは実際に起きた不具合で（[X56](EXPERIMENTS.ja.md)）、
症状は「500 us刻みのはずが1,000 us刻みになる」という分かりにくいものだった。
既製portはこの種の取りこぼしも同時に潰す。

### 提案5: 公開面を役割で分ける

40関数を1枚に並べるのをやめ、役割ごとに区切る。

| 区分 | 例 | 対象 |
| --- | --- | --- |
| 組み立て | `bindWireDevice`, `setChannelHandler` | 利用者 |
| 実行窓 | `runBegin`, `runEnd`, `armRunWindow` | 利用者 |
| 世界を動かす | `chanWrite`, `pinInject`, `frameTx` | 利用者 |
| 結果を読む | `formatTrace`, `stats`, `nowUs` | 利用者 |
| 観測者 | `addListener`, `setTickHandler` | 上級 |
| 内観 | `eventBytes`, `deferralCapacity` ほか5つ | **実験専用**。別ヘッダへ |

内観7つを別ヘッダ（`embedbench_internals.h` など）へ移せば、
公開面は40→33に減り、**利用者が読むべきものだけが残る**。

### 提案6: 模型は第2のライブラリとして出す

`devices/` は既にArduinoライブラリの体裁を持ち、
`libraries: dir` で参照されている。これを最上位へ上げて
独立したライブラリ（例: `EmbedBenchDevices`）にする。

**`src/` へ入れてはならない。** Arduinoは `src/` 配下の全 `.cpp` を
再帰的にコンパイルするので、模型23ファイルが全利用者のビルド時間に乗る。
使う人だけが入れられる形が正しい。

## 4. 凍結IFへの変更提案（承認が必要）

提案4の効果を最大化するには、凍結IFへ1点の変更が要る。
[凍結記録](DEVICE_IF_FROZEN.ja.md)の規則どおり、根拠を添えて承認を求める。

**変更内容:** `HostPort::lineOut` と `HostPort::serialOut` の
`= 0`（純粋仮想）をやめ、他の任意メソッドと同じく既定実装を与える。
`lineOut` は何もしない、`serialOut` は `false`（＝受け取らなかった）を返す。

**根拠（実測）:** この2つの実装30件のうち、`serialOut` は20件、
`lineOut` は18件が中身の無い定型文だった。しかも `serialOut` の空実装は
`true` と `false` に割れており、**規定が無いために書き手が迷っている**。
既定を置けば迷いも定型文も消える。

**影響:** 追加ではなく既存メソッドの性質変更だが、
**既存の実装は1つも壊れない**（overrideし続けられる）。
IFの版は上げず、revisionを005へ進める。

## 5. 決めていただきたいこと

1. 名前空間を `ebhost::` にするか、`ebd::` のままか（提案3）
2. 模型を第2ライブラリとして出すか、`tests/` に留めるか（提案6）
3. 凍結IFの純粋仮想2つに既定を与えるか（4節。**承認事項**）
4. 公開時点で環境APIも凍結するか、「発展中」と明示して出すか

1〜2は好みの範囲だが、**今しか安く変えられない**という点だけ申し添える。
