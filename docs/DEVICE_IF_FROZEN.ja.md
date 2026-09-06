# デバイスIF 凍結記録（version 1）

内部の記録。日本語のみ。**これは決定台帳である**——候補や実験結果ではなく、
確定した事項だけを書く。凍結日: 2026-09-04。対象: `src/embedbench_device.h`。

工程の正本は[DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md)、範囲と理由は
[DEVICE_IF_SCOPE.ja.md](DEVICE_IF_SCOPE.ja.md)、根拠の実測は
[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md)（X12・X19〜X41）にある。

## 1. 凍結したもの

`ebdev` 名前空間の次の面を version 1 として固定する。

| 区分 | 面 |
| --- | --- |
| 版数 | `kDeviceInterfaceVersion`（= 1）、`kDeviceInterfaceRevision`（= 004） |
| 定数 | `kFormatNameMaxLength`（19）、`kChannelUnsupported` |
| 型 | `I2cStatus`（0〜4）、`I2cTransfer{stop, continued}` |
| helper | `frameBytes`、`framePaddingClean`、`schemaFingerprint` |
| `HostPort` | `nowMicros` / `lineOut` / `serialOut` / `frameOut` / `formatId` / `maxFrameBits` / （rev1）`analogOut` / `analogOutMilliVolts` / `requestWake` / `diagnose` |
| `Device` | `reset` / `attach` / `i2cWrite` / `i2cRead` / `spiTransfer` / `serialIn` / `lineIn` / `frameIn` / `channelWrite` / `channelRead` / `advanceTo` / `dump` |

契約（ヘッダ本文が正）: 再入禁止（効果あり8経路）、effect-free 3経路、時間、
借用バッファ、リンク所有、frameのMSB-first packingと原子性、format名と
schema指紋、サイズのネゴ、`serialOut`の全量/診断つき部分配送。

## 2. 凍結の根拠

| 主張 | 根拠 |
| --- | --- |
| プラットホーム非依存で書ける | X23（`g++ -std=c++11 -Werror`単体ビルド、Arduino・host core非依存を機械検査） |
| 同一ソースが複数環境で同一挙動 | X23（host）、X29（環境実装例#2）、以後の全実験がnative/host両方で一致 |
| 4類型のデバイスを表現できる | register-map（X18/X30）、command（X21）、SPI複合（X24）、frame型（X25/X31） |
| 未対応プロトコルに拡張口がある | X25/X26/X31（format名＋schema、bus id、bit packing） |
| 異常系が定義されている | X31/X39/X41（padding・戻り長・容量不足）、X32/X35（再入と延期容量）、X34（名前衝突・長さ） |
| 環境側の負担が小さい | 環境実装例#2が実効LOC 375、host adapterが33行 |

## 3. 変更規則

1. version 1 はソース互換・挙動互換を保つ
2. 追加は「既定実装つきの新しい仮想関数」「新しい定数」「新しいhelper」に限る
3. 既存のシグネチャ・既定値・契約を動かす変更は version 2 とし、version 1 と
   同じやり方（先に実測）で決める
4. `kDeviceInterfaceVersion` は 3 の変更でのみ上げる
4.5. **revisionは3桁の連番**（現在004、`kDeviceInterfaceRevision`）。承認された
   追加ごとに1つ上げ、ためこまない。001が0.0.1相当、**100がリリースの目印**。
   上げるのはメンテナーの承認後だけで、実測を添えて求める
5. `tests/if_frozen/` が面を固定する。意図しない変更はテストが落ちる
6. 変更の理由は必ず[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md)へ実測とともに残す

### 凍結の意味 — 「変えない」ではなく「無断で変えない」

以後の検証で、このIFではうまく扱えない事象に当たったとき、**環境側や模型側の
周辺ロジックで回避することを既定にしない**。次の順で判断する。

1. まず「IFを変えた方が仕様として単純になるか」を検討する
2. 単純になるなら、そう言える実測（回避策と比較した記述量・分岐数・
   契約の増減）を取る
3. その実測を添えて**メンテナーへ変更の承認を要求する**。承認されれば
   追加（version 1のまま）か version 2 として実施する
4. 承認されない場合にかぎり、周辺ロジックで対処し、その判断と理由を台帳へ残す

設計上の問題を隠した回避策は、意図的な版上げより高くつく。凍結は変更の
ハードルであって、禁止ではない。

## 3.5 revision 002〜004（メンテナー承認済み、2026-09-06）

メンテナーの提起「アナログなど足りていないIFは追加した方がよいのでは」を受け、
3節の手順どおり**回避策と比較した実測**（`tests/if_gaps/`、X47）を取り、
変更内容を提示して**承認を得てから**追加した。いずれも既定実装つきの新しい
仮想関数で version 1 のまま（ソース互換・挙動互換）。

| revision | 追加 | 何が無いと困るか | 実測（回避策 → 追加後） |
| ---: | --- | --- | --- |
| 002 | `HostPort::analogOut(line, raw)` / `analogOutMilliVolts` | センサーが提示する電圧をデバイス自身が出せない。回避策は進行役が毎回 `channelRead` で吸い出して注入すること | アプリが読む値 0 →（進行役1手）→ 1234 / **進行役0手で1234** |
| 003 | `HostPort::requestWake(whenUs)` | 環境のtick境界でしかデバイスが進まないので、tickで割り切れない遅延が遅配される | 1,500us遅延・tick 1,000usで **2,000us（500us遅い）→ 1,500us（定刻）** |
| 004 | `HostPort::diagnose(text)` | 戻り値のない経路（serial等）でデバイスが気づいた違反を言う手段がない | ログに残る件数 0 → **1（発生時点・順序どおり）** |

設計上の性質:

- すべて既定が「この環境は経路を持たない」（`false`）。revision 001 に対して
  書かれた模型・環境はそのまま動く
- `analogOut` の raw と mV は分離（換算にはattenuationとVrefの模型が要り、
  環境は持たないため）
- `requestWake` は要求であって命令ではない。環境は要求より頻繁に進めてよく、
  受け付けない環境は `false` を返す
- `diagnose` は**効果ではなく注釈**。世界を変える用途に使ってはならない

両環境（draft core、環境実装例#2）へ実装し、同一の結果になることを確認した
（`tests/if_rev1/` と `tests/if_gaps/` の共有環境ケース）。

## 4. 凍結の対象外（IFの外）

- 環境側の実装（host hookの所有、時計、記録、ログ形式、診断イベント名）は
  **プラットホーム別の実装例**であり凍結しない。現在の例は
  `src/embedbench_draft.*`（host-arduino-core）と `tests/common_env/`（純粋C++）
- 集約記録の適用基準とchecksum種別（X28の未決）
- bus別 `maxFrameBits` 差がある環境でのformat側分割規則（X27の未決）
- デバイスカタログ、format名の登録運用（プロジェクト運用の課題）

## 5. 凍結後の次段階

1. **環境の準拠キット**: IFの契約を*デバイス側から*検証する共通probe模型を作り、
   どの環境実装例でも同じ判定が出ることを確認する（環境が増えたときの受け入れ試験）
2. 実装例の整備: draft coreへAnalogとlifecycle連動のrun windowを追加
3. ログ側の課題（X28の未決、1行形式のparse/diff比較）
