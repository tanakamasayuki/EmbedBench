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
| 版数 | `kDeviceInterfaceVersion`（= 1） |
| 定数 | `kFormatNameMaxLength`（19）、`kChannelUnsupported` |
| 型 | `I2cStatus`（0〜4）、`I2cTransfer{stop, continued}` |
| helper | `frameBytes`、`framePaddingClean`、`schemaFingerprint` |
| `HostPort` | `nowMicros` / `lineOut` / `serialOut` / `frameOut` / `formatId` / `maxFrameBits` |
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
5. `tests/if_frozen/` が面を固定する。意図しない変更はテストが落ちる
6. 変更の理由は必ず[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md)へ実測とともに残す

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
