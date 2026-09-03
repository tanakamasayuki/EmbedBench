# ドキュメント案内

この下には、設計中の内部記録・実験結果・判断理由を日本語のみで置く。
公開仕様が固まり、利用者向けの文書になったものは英語版と日本語版を用意して
相互リンクする。

## 言語ルール

| 区分 | 言語 | 置き場所の例 |
| --- | --- | --- |
| 変更履歴 | 1ファイル内に英語と日本語 | `../CHANGELOG.md` の `(EN)` / `(JA)` |
| 利用者向け文書 | 英語と日本語を別ファイルにして相互リンク | `README.md` / `README.ja.md`、`tests/README.md` / `tests/README.ja.md` |
| 未確定の設計・実験記録 | 日本語のみ | `docs/*.ja.md` |
| コード中のコメント | 原則として英語のみ。必要なら英語と日本語 | `src/`、`tests/`、`tools/` |

現在は公開APIも構成も未確定なので、ここへ追加する設計文書は日本語のみとする。
コードから日本語の設計説明へリンクする必要がある場合も、コード側のコメント自体は
英語で記述する。

## 文書

- [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md): 工程、承認ゲート、作業分割、完了条件
- [EXPERIMENTS.ja.md](EXPERIMENTS.ja.md): hostで確認した数値、候補、未決事項の台帳
- [EVENT_MATRIX.ja.md](EVENT_MATRIX.ja.md): 操作経路表（WP-A1）。全操作の記録点・応答担当・注入・診断の候補
- [DEVICE_IF_SCOPE.ja.md](DEVICE_IF_SCOPE.ja.md): デバイスIFの範囲と線引き。専用portとframeの分担、カバー範囲、絶対にやらない領域
- [HOST_EXTENSION_AUDIT.ja.md](HOST_EXTENSION_AUDIT.ja.md): host coreだけで可能な範囲と追加依頼案
- [reference/tinygfx-emu/SOURCE.ja.md](reference/tinygfx-emu/SOURCE.ja.md): TinyGFXから一度だけ取り込んだ初期案の出典
