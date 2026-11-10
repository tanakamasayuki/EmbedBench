# ドキュメント案内

## 言語ルール

| 区分 | 言語 | 置き場所の例 |
| --- | --- | --- |
| 変更履歴 | 1ファイル内に英語と日本語 | `../CHANGELOG.md` の `(EN)` / `(JA)` |
| 利用者向けガイド | 英語と日本語を別ファイルにして相互リンク | `README.md` / `README.ja.md`、`tests/README.md` / `tests/README.ja.md` |
| 設計の説明 | 日本語のみ | `docs/*.ja.md`（英語版の無いもの） |
| コード中のコメント | 原則として英語のみ。必要なら英語と日本語 | `src/`、`tests/`、`tools/` |

コードから日本語の設計の説明へリンクする場合も、コード側のコメント自体は英語で書く。

## 利用者向けガイド（英語・日本語）

- [FACTS.ja.md](FACTS.ja.md) / [FACTS.md](FACTS.md): **現状の数値。外部文書はここを引用する。**
  テストで固定されているので古くならない
- [GUIDE.ja.md](GUIDE.ja.md) / [GUIDE.md](GUIDE.md): **入門ガイド。まずこれ。**
  何のためのものか、3つの登場人物、記録の読み方、最初のテストの書き方
- [ADVANCED.ja.md](ADVANCED.ja.md) / [ADVANCED.md](ADVANCED.md): **上級ガイド。**
  デバイス模型の書き方、守るべき契約、frame経路、環境実装の落とし穴

## 設計の説明（日本語のみ）

- [DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md): デバイスIF（version 1）の凍結した面と変更規則。変更規則の正本
- [DEVICE_IF_SCOPE.ja.md](DEVICE_IF_SCOPE.ja.md): デバイスIFの範囲と線引き。専用portとframeの分担、カバー範囲、絶対にやらない領域
- [DEVICE_CATALOG.ja.md](DEVICE_CATALOG.ja.md): 模型の置き場所、format名の登録運用、新しい模型を追加する手順
- [REAL_DEVICE_DATA.ja.md](REAL_DEVICE_DATA.ja.md): 実デバイスの記録から模型を作り、確かめる流れ。行の形、道具、残っている仕事
- [HOST_CORE.ja.md](HOST_CORE.ja.md): host core（lang-ship:host）で使うときの決まりと、CH32 の core の API への対応
