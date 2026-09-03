# Changelog / 変更履歴

## Unreleased

- (EN) Added the initial Arduino library structure and a host smoke test covering library resolution, lifecycle hooks, and a virtual clock.
- (JA) Arduino ライブラリの初期構成と、ライブラリ解決・ライフサイクルフック・仮想時計を確認する host smoke テストを追加。
- (EN) Added host experiments for clock granularity, lifecycle order, peripheral ports, and fixed tick boundary splitting.
- (JA) 時計の粒度、ライフサイクル順序、周辺機能ポート、固定 tick 境界への分割を調べる host 実験を追加。
- (EN) Documented the development stages, approval gates, logging-completeness boundary, and independently assignable work packages before defining the public specification.
- (JA) 公開仕様を定める前の開発段階、承認ゲート、ログ完全性の境界、独立して担当できる作業単位を文書化。
- (EN) Imported the TinyGFX emulation draft as an immutable reference and documented host-core capability gaps with reproducible measurements and extension-request candidates.
- (JA) TinyGFXのemu初期案を変更しない参考資料として取り込み、host coreの能力不足を再現可能な実測値と追加依頼候補に整理。
- (EN) Limited the initial execution model to standard Arduino-style setup/loop and synchronous callbacks, excluding FreeRTOS and esp_timer virtualization.
- (JA) 初期の実行モデルを標準的なArduinoのsetup/loopと同期callbackに限定し、FreeRTOSとesp_timerの仮想化を対象外にした。
- (EN) Added five host experiments answering the ledger's open questions: 0 us wait re-entry, wait APIs called inside a tick, listener fan-out limits, event buffer overflow policies with line-format sizes, and Wire observer/responder separation.
- (JA) 実験台帳の未解決項目に答える5つのhost実験を追加: 0 us waitの再入、tick内からの待ちAPI呼び出し、listener配送の上限、イベントバッファ満杯方式と1行形式の容量、Wireの観測者・応答者分離。
