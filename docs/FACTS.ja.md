# 現状の数値

> English: [FACTS.md](FACTS.md)

他の文書が引用するための数値。ここの数字は `tests/facts/` が検査するので、
古くなったものはテストで落ちる——他所の文書へ古い値が渡っていくことはない。

**自分で数えずにこのページを引用してください。** このプロジェクトの状態は動き、
直近3件の外部からの引用は、いずれも1つ以上の数字が古かった。

<!-- FACTS BEGIN -->
| 項目 | 値 |
| --- | ---: |
| デバイスIF version | 1 |
| デバイスIF revision | 4 |
| デバイスIFの実効LOC | 140 |
| `devices/` のデバイス模型 | 25 |
| 環境実装 | 2 |
| 実験ディレクトリ | 67 |
| SDカードのプリセット | 7 |
<!-- FACTS END -->

実効LOCは空行とコメントのみの行を除いた数で、台帳と同じ基準。

**大きさはここに載せない。** 生の行数は誰かがコメントを直すたびに動くので、
固定すると**普通の編集がビルドを落とす**。実際に一度そうなった。
行数で答えようとしていた問いには、それぞれ良い答えがある。

| 問い | 見るもの |
| --- | --- |
| 模型はfirmwareに載るか | `devices/tools/measure_footprint.sh` — 模型ごとのRAMと `.text`（Cortex-M0+ / RV32） |
| 環境は膨らんでいないか | `tests/native_env/` と `tests/device_if/` のLOC固定値。増加の履歴つきで、意図的に更新する |

## 固定されているもの、いないもの

**固定:** `src/embedbench_device.h`。デバイス模型と任意の環境との境界。
純粋C++11、プラットホームヘッダ無し、`<stddef.h>` と `<stdint.h>` のみ。
変更にはメンテナーの承認が要る（記録と規則は
[DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md)）。

**固定でない:** それ以外すべて。host環境（`src/embedbench_host.*`）は
環境実装の1つであり、名前も構成もまだ動きうる。ただし**静かには変わらない**——
変更は必ず [CHANGELOG.md](../CHANGELOG.md) に残る。

## 置き場所

| パス | 中身 |
| --- | --- |
| `src/embedbench_device.h` | 凍結IF |
| `src/embedbench_host.*` | host Arduino環境 |
| `src/embedbench_internals.h` | 実験用の内観 |
| `devices/` | デバイス模型（第2のArduinoライブラリ） |
| `devices/tools/` | 生成器: キャプチャ→表（`trace2regtable.py`）、→tape（`trace2tape.py`）、ロジックアナライザ→行（`la2trace.py`）、SDイメージ |
| `capture/` | 実機側のキャプチャシム（第3のArduinoライブラリ、`CaptureWire`） |
| `tests/common_env/` | 純粋C++環境（`nenv`） |
| `tests/conformance/` | 環境の受け入れ試験 |

模型を `src/` の外に置いているのは意図的で、Arduinoがライブラリの `src/` 配下を
再帰的に全部コンパイルするため。分けておけば、使うスケッチだけがその費用を負う。

## 準拠キット

`tests/conformance/` が環境の受け入れ試験。1つのprobeと1つのシナリオを走らせ、
既存2実装が同じ判定に達することを確認する。第3の環境は、その上に何かを積む前に
これを通すべきである。

**必須**（欠けると判定が落ちる）:

| 検査 | 何を保証するか |
| --- | --- |
| `kCheckNoReentry` | 動作中のデバイスへ再入しない |
| `kCheckTimeMonotonic` | `advanceTo` が逆行しない |
| `kCheckNowAgrees` | `nowMicros()` が `advanceTo` に渡した時刻と一致する |
| `kCheckBorrowedBuffer` | 渡したバッファがその呼び出しの間有効 |
| `kCheckFrameAccepted` | 正しいフレームは丸ごと配送される |
| `kCheckFrameOversizeRefused` | 超過フレームは切り詰めず拒否 |
| `kCheckFormatStable` | 同じformat名は常に同じidになる |
| `kCheckFormatNameLimit` | 長すぎるformat名は拒否 |

**観測はするが必須でない**——準拠環境でも出るとは限らないので、記録するが落とさない:

`kCheckTimeRepeat`（契約は同時刻の再呼び出しを許すが要求はしない）、
`kCheckAnalogRouted`、`kCheckWakeHonored`、`kCheckNoteRouted`
（承認済み3経路は任意で、提供しない環境も準拠している）。

**キットが覆っていないもの**。実機上の環境では効いてくる:

- **同一時刻に落ちた効果の順序。** 既存2実装はデバイスを固定順で回すので
  決定的だが、第3の実装がそうである保証は検査していない。
- **記録が溢れたときの振る舞い**（畳み込み、切詰め通知）。
- **診断の語彙**（`diag.unbound` など）。ここが食い違っても両方とも準拠だが、
  **トレースを行単位で比較することはできなくなる**。
- **模型側の契約違反**（`reset` / `channelRead` / `dump` が `HostPort` を呼ぶ）。
  キットは環境を検査するもので、模型は見ていない。模型側の検査は
  `tests/device_if/` にある（X62で3件の違反が実際に見つかった）。

## 引用する人への注意

- **模型の時間定数には基準が書いてある。** ほとんどは実物の値だが、6つは
  意図的に圧縮してある（実物の値だと仮想時計のテストが数分に及ぶため）。
  各定数がどちらかを明記しており、書かれていなければ `tests/device_if/` が落ちる。
  **圧縮された定数からタイミングの結論を引き出さないこと。**
- **トレースの行数はイベント数ではない。** 記録が満杯になると繰り返し周期を
  1周＋回数へ畳むので、数万イベントを数十行で欠落なく運ぶことがある。
  `stats()` は `events` / `folded` / `dropped` を別々に報告する。
- **`stats().windows == 0` は `runBegin` を呼んでいないという意味**で、
  「何も起きなかった実行」とは別物。
