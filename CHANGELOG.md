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
- (EN) Drafted the Gate A operation-path matrix (WP-A1) with an actor diagram and a hand-written I2C sequence, and measured the three direct-call routing policies (WP-A2) with one register-map device model.
- (JA) Gate Aの操作経路表（WP-A1）をアクター図と最小I2Cシーケンス手書き例付きで起草し、直接呼び出し3案（WP-A2）を同一のregister-map模型で計測。
- (EN) Moved all experiments to host core 1.7.1 and verified its new extension ports resolve the H1-H3 audit requests: the interrupt port (with the raw-mode numbering trap documented), the synchronous UART activity hook (zero-wait in-hook replies), and the analog millivolt/read-width hooks.
- (JA) 全実験をhost core 1.7.1へ移行し、新しい拡張口が監査依頼H1〜H3を解決することを検証: 割り込み口（生mode定数の罠も記録）、同期UART activity hook（hook内応答でwait 0回）、Analog mV/分解能hook。
- (EN) Proved three vertical slices as candidates: interrupt injection through edge decision and ctx=isr tagging, a UART AT-conversation golden with immediate and tick-delayed replies, and the WP-C1 I2C slice driving an unmodified app with zero event loss and byte-identical traces across three runs.
- (JA) 3本の縦切りを候補として実証: 線注入からedge判定と`ctx=isr`付与までの割り込み経路、即時応答とtick遅延応答を持つUART AT会話golden、無改造アプリをイベント欠落0・3回byte一致で駆動するWP-C1のI2C縦切り。
- (EN) Reworked the Gate A materials after review: event-completion timing and completeness scope became explicit open decisions (with rejected-operation visibility measured as evidence), device-originated GPIO/Analog/UART effects were unified through core sinks, wait events were redefined as host wait requests rather than API names, and bus-instance scope plus the run-window end condition were pinned down.
- (JA) レビューを受けてGate A資料を改訂: イベント完成タイミングと完全性の範囲を明示的な未決へ変更（拒否操作の可視性を実測して根拠化）、dev発のGPIO/Analog/UART作用をcore sink経由へ統一、waitイベントの意味を「hostのwait要求」へ再定義、対象bus instanceの範囲と実行区間の終了条件を明文化。
- (EN) Measured the four event-completion policies under a re-entrant sink call (only request/response splitting keeps causality visible and stream order intact) and redid the UART conversation with device replies recorded through the core RX sink.
- (JA) 再入sink呼び出し下でイベント完成4方式を計測（因果の可視化とstream順序を両立するのは要求/応答2行分割のみ）し、dev応答をcore RX sink経由で記録するUART会話へ作り直し。
- (EN) Entered the implementation-first phase by owner decision: integrated the measured winners into a draft core (src/embedbench_draft.*, explicitly unapproved and rework-expected) and measured a multi-bus scenario — GPIO, an interrupt, I2C, UART, and virtual time on one 21-event list, byte-identical across three runs.
- (JA) 所有者判断で先行実装フェーズへ移行: 実測の最優秀候補を統合したdraft core（src/embedbench_draft.*、未承認・手戻り前提）を実装し、GPIO・割り込み・I2C・UART・仮想時間を21イベント1本の列に載せて3回byte一致を計測。
- (EN) Narrowed the surface to be fixed to the portable device interface (src/embedbench_device.h, pure C++11): the same unmodified register-map and command models pass a g++-only native build with -Werror and drive the host environment through a 32-line adapter, with everything above the interface reclassified as per-platform implementation examples.
- (JA) 固定対象をポータブルなデバイスIF（src/embedbench_device.h、純粋C++11）へ絞り込み: 同一無改変のregister-map型・command型模型が-Werror付きg++単体ビルドを通り、32行のadapterでhost環境も駆動。IFより上は プラットホーム別実装例へ再分類。
- (EN) Validated the interface against a composite SPI display model (data/command input line, busy output line, time-released busy): lineIn() was the only addition needed (+4 lines), and recorded the owner's abstraction policy — the library carries logical bitstreams plus format information, never physical-layer reproduction.
- (JA) 複合SPIディスプレイ模型（DC入力線・busy出力線・時間解除）でIFを検証: 必要な追加は lineIn() のみ（+4行）。「本ライブラリは論理ビット列＋フォーマット情報までを受け渡し、物理層は再現しない」という抽象度方針も記録。
- (EN) Added the generic frame path (HostPort::frameOut / Device::frameIn: a format id plus pre-encoding logical bits of any bit count) so protocols without a dedicated port never fall back to pin-level bit-banging, verified native and on-host with an addressed remote-node model.
- (JA) 汎用frame経路（HostPort::frameOut / Device::frameIn: format id＋符号化前の任意bit数論理ビット列）を追加し、専用portのないプロトコルがビットバンへ落ちない道をアドレス付き無線ノード模型でネイティブ・host両検証。
- (EN) Settled format identity by measurement: fixed numbers collide silently between independent libraries, so names are the identity and environments intern them (HostPort::formatId, one resolution then integer compares; strings-only costs one strcmp per frame); frames also gained a device-local bus id, and the device-interface scope document draws the dedicated-port/frame split and the never-touch boundaries.
- (JA) format識別を実測で決着: 固定番号は独立ライブラリ間で静かに衝突するため、名前を識別子とし環境がinternする方式を採用候補に（HostPort::formatId、解決1回で以後整数比較。文字列のみは毎frame strcmp）。frameへデバイス論理bus idも追加し、専用portとframeの分担・絶対にやらない領域をDEVICE_IF_SCOPE.ja.mdへ明文化。
- (EN) Made size limits a negotiation, not a constant: HostPort::maxFrameBits(bus) lets one unmodified model auto-split (4 frames on a 64-bit port, 1 on a 4096-bit port), oversized calls are rejected whole with a diagnostic, and framebuffer-scale SPI transactions coalesce into one count-plus-checksum summary instead of 200 dropped per-byte records.
- (JA) サイズ上限を定数でなくネゴに: HostPort::maxFrameBits(bus)で同一無改変模型が自動分割（64bit環境で4 frame、4096bitで1 frame）、上限超過は診断付きで全量拒否。フレームバッファ規模のSPI transactionは件数+checksumのサマリ1行へ集約（per-byte方式は146 dropped の爆発を実証）。
