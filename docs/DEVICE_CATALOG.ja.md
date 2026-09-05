# デバイス模型カタログと format 名の運用

内部の記録。日本語のみ。凍結したデバイスIF（[DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md)）で
書いた模型を、どこに置き、どう名前を付け、どう検証するかを定める。
IFの外側の**運用**であり、IFの変更を伴わない。

## 1. 模型の置き場所

| 置き場所 | 用途 | 例 |
| --- | --- | --- |
| `tests/common_models/src/` | 複数の実験・複数の環境から共有する参照模型 | 温度センサ、ATモデム、register-map、準拠probe |
| `tests/<実験名>/` | その実験でしか意味を持たない模型 | 契約違反を仕込んだ `badlen_model`、容量試験の `flood_model` |

判断基準は「2つ目の利用者が現れたか」。現れた時点で `common_models/` へ移す
（X29で参照模型を移した時と同じ手順: `sketch.yaml` の `libraries: dir` と
ネイティブ側の `-I` を共有先へ向ける）。

模型が満たすこと:

- includeは `embedbench_device.h` と C標準ライブラリのみ
- `reset()` / `channelRead()` / `dump()` は effect-free（`HostPort`を呼ばない）
- 時間依存はすべて `advanceTo()` 駆動。プラットホームのタイマを使わない
- ネイティブで単体検証できる（`g++ -std=c++11 -Wall -Wextra -Werror`）

## 2. format 名の登録運用

frame経路の format 名は**ライブラリ間で衝突しない識別子**であり、番号は環境
ローカル（X26/X34）。運用規則:

1. 形式は `<vendor>.<protocol>.<version>`、**最大19文字**（`kFormatNameMaxLength`）。
   超過は登録が拒否される（切り詰められない）
2. `<vendor>` は実在の主体名か、その略号。プロジェクト内の実験模型は `acme.` を
   使う（実在ベンダーと衝突しない架空の名前として固定）
3. `<version>` はレイアウトの版。**レイアウトを変えたら名前の版を上げる**。
   同名のまま変えると登録時にschema衝突として拒否される
4. schema指紋は `schemaFingerprint("u8 addr,u8 cmd")` のように**レイアウトを
   説明する短い文字列**から生成する。手書き定数でもよいが、レイアウトと指紋が
   ずれないよう同じ場所に書く
5. 指紋は32bit hashなので、**異なるレイアウトが同一指紋になる可能性は残る**。
   衝突検出は「指紋が異なる場合に働く」ものであり、完全な保証ではない

現在使われている名前:

| 名前 | レイアウト | 使用箇所 |
| --- | --- | --- |
| `acme.node.1` | u8 addr, u8 cmd | `frame_port/`、`format_registry/` |
| `acme.tele.1` | u8 addr, u8 power | 同上 |
| `acme.ir.1` | 12bit command | `frame_bits/` |
| `acme.irstat.1` | 128bit status | 同上 |
| `acme.bulk.1` | u8 index, u8 total, data | `capacity/` |
| `acme.snap.1` | 128bit snapshot | 同上 |
| `acme.stat.1` / `acme.cmd.1` | u8 hi,u8 lo / u8 cmd | `reentry_paths/` |
| `acme.probe.1` | u8 probe | 準拠probe |

## 3. 新しい模型を追加する手順

1. 模型を書く（IFのみに依存、上記の条件を満たす）
2. ネイティブ単体で検証する（FakePortか `common_env/` の環境実装例）
3. host環境でも同じ模型を無改変で動かす（adapterは実験側に置く）
4. 2つ目の利用者が現れたら `common_models/` へ移す
5. frame経路を使うなら、2節の名前を台帳の表へ追加する

## 4. 準拠キットとの関係

[tests/conformance/](../tests/conformance/) の probe は**環境**を検査する道具で、
模型を検査するものではない。新しい**環境**を追加したときは準拠キットを通す。
新しい**模型**を追加したときは、その模型自身のネイティブ試験を書く。
