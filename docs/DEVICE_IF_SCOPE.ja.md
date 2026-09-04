# デバイスIFの範囲と線引き

内部の記録。日本語のみ。固定対象である `src/embedbench_device.h` について、
何を専用portにし、何をframeに載せ、どこまでをカバーし、どこから先は他の
テスト手段に任せ、そして**何には絶対に手を出さないか**を定める。
根拠は[EXPERIMENTS.ja.md](EXPERIMENTS.ja.md)のX12・X19〜X34。
IFの契約（再入・時間・借用バッファ・リンク所有・bit packing・原子性・
channel/dumpの戻り値）はヘッダ本文に明記してあり、本書はその範囲と理由を扱う。

## 1. 経路の分類 — 専用portとframeの分担

| 分類 | 経路 | 性質 | 例 |
| --- | --- | --- | --- |
| 同期要求・応答bus | `i2cWrite`/`i2cRead`（`I2cTransfer`付き）、`spiTransfer` | masterがブロックして戻り値（status / bytes / MISO）を待つ。Arduino標準APIが型付けし、host hookから同期callbackで届く | register-mapセンサ（X30）、SPIディスプレイ（X24） |
| byte stream | `serialIn` / `serialOut` | 非同期の一般的なbyte列。戻り値ではなく後続の出力で応答する | ATモデム（X21/X23） |
| デバイスの外部作用・環境入力 | `lineOut` / `lineIn`、`channelWrite` / `channelRead`、`advanceTo` | 通信busではない。線の電位、世界の物理量、時間 | DRDY/IRQ線（X16/X32）、温度注入（X18） |
| それ以外の論理プロトコル | `frameIn` / `frameOut`（bus id + format id + 論理bits） | 専用portのない全プロトコルの拡張経路 | IRリモコン（X31）、無線ノード（X25/X26） |

**SPI/I2Cが専用portである理由:** frameへ統一すると、host hookが型付きで渡す
データを直列化して模型側で戻すだけの往復変換になる（X12で型変換1往復=2回を
実測）うえ、同期の戻り値を一方向のframeで表すには応答frameとの対応付けを
別途発明する必要がある。UARTは戻り値を待たないが、Arduino標準の型付きbyte
streamとして専用portに置く（条件は「同期応答」ではなく「標準APIが型付けする
交換」）。

**打ち止め条件（原則frame）:** 新しいプロトコルは原則frame経路に載せる。
専用portの追加を再検討するのは次の3条件がそろった場合だけとする。

1. 同期の戻り値が本質的に必要である
2. frameによる要求/応答の対応付けが模型を不自然にする
3. 複数の実デバイス類型で必要性が確認できた

X25/X26/X31で確認したframeの利用は一方向要求＋遅延応答までであり、同期応答・
双方向同時通信・stream型プロトコルは未検証。この範囲でframe経路を「原則」とする。

**PIO・赤外線などをframeへ載せる前提:** アプリ側またはそのprotocolライブラリの
host版shimが符号化前の論理frameを環境へ渡す必要がある。GPIO波形しか出さない
無改造ライブラリを環境が自動的にframe化することはできない（波形デコードは
3.3節の「絶対にやらない」）。

## 2. frameの構成要素と契約

| 要素 | 決定 | 根拠 |
| --- | --- | --- |
| bus | `uint8_t`、デバイス論理番号。実リンク対応はbinding側 | 同一プロトコルの複数リンク（PIOの複数SM、複数IRチャネル、複数CAN）を区別（X26） |
| format | `uint16_t`、環境がinternしたid。名前は `<vendor>.<protocol>.<version>`、登録時に**schema指紋**を照合し、同名・異schemaは衝突として拒否 | 固定番号は独立ライブラリ間で静かに衝突（X26）。名前だけでも同名を選べば衝突するため、schema照合で「同名・異仕様」を検出（X34）。解決は1回でキャッシュ、以後は整数比較 |
| bits + data | MSB-first packing（frame bit 0 = data[0]のbit 7）、末尾の未使用bitは0、bits=0は空frame（trigger）でdata=nullptr可 | packingを定めないと同じ模型が環境ごとに別の動作になる。汚れたpaddingは環境が拒否（X31） |
| 原子性 | `maxFrameBits(bus)`は「原子的に送れる最大frame」。上限内は全量受理を保証、超過は`frameOut`がfalseを返し環境が診断。**模型は自動分割しない**。分割はformat自身が定義する場合だけ（各frameがそのformatの完全なframe） | 1つの128bit commandを64bit×2に割ると別のframeになる。X27では分割規則を持つ`acme.bulk.1`と分割不能な`acme.snap.1`を対比 |
| サイズ上限の所在 | 環境の性質（記録バッファ、MTU）なのでヘッダに定数を焼き込まず環境とネゴする。既定0=frame経路なし | X27 |
| インバウンド | ネゴ不要。データはcall中のみ有効な借用で、デバイスは必要分だけ読む | ヘッダの借用バッファ契約 |

## 3. ユースケースと対応

### 3.1 このライブラリがカバーする

| ユースケース | 経路 | 根拠 |
| --- | --- | --- |
| I2C register-map / commandデバイス（repeated start要求を含む） | 専用port + `I2cTransfer` | X18/X23/X30 |
| SPIデバイス（DC/CS線つき複合を含む） | 専用port + `lineIn` | X24 |
| UART会話デバイス（遅延応答含む） | 専用port + `advanceTo` | X21/X23 |
| デバイスのIRQ・DRDY・busy線（応答中の発生を含む） | `lineOut`（環境が再入を延期） | X16/X24/X32 |
| センサー値・環境量の注入と検分 | channel（戻り値契約つき） | X12/X18/X33 |
| IR・sub-GHz・PIO・WS2812など未対応プロトコルの論理frame | frame経路 | X25/X26/X31 |
| 応答遅延・周期送信などの時間駆動挙動 | `advanceTo`（時間契約つき） | X17/X21/X33 |
| 証拠dump | `dump`（snprintf規約） | X18/X33 |

### 3.2 ここまではカバーし、その先は他のテストが好ましい

| ユースケース | ここまでカバー | その先は |
| --- | --- | --- |
| 大量ストリーミング（フレームバッファ、音声ブロック） | 内容は運ぶ。goldenにはtransaction単位の件数・checksum要約を残す（X28） | ピクセル単位の描画検証は描画ライブラリ側のテスト（TinyGFX等）で |
| プロトコルのbit列⇔波形の符号化/復号（NEC IR、Manchester、CRC） | 符号化**前**のbitsをframeで受け渡すところまで | 純関数として切り出して単体テスト |
| デバイス模型の内部アルゴリズム | IF越しの入出力の因果まで | 模型自体のネイティブ単体テスト（X23/X33の形） |
| バス設定の妥当性（clock速度、mode） | 「何が設定されたか」の記録まで | タイミング適合の判定は実機・専用ツール |
| 複数masterやbus arbitration | 単一masterの決定的順序まで | 実機または専用シミュレータ |
| 複数のI2C/SPI/UARTを持つ複合デバイス | 1 Deviceは各専用busを最大1つ。複合はadapterが子Deviceへ分割 | — （制限として明記、ヘッダ契約） |
| ISR内での同一bus使用 | デバイスは再入されない。アプリ側の受信バッファ破壊は実機と同種のバグとして**そのまま観測**される（X32: app値がFFFF） | アプリ設計の問題として修正する |

### 3.3 絶対に手を出さない

- **物理層・波形・電気特性**: 電圧、駆動能力、プルアップ、スルーレート、
  ノイズ、信号品質
- **サイクル精度・実時間忠実性**: クロック周波数の再現、setup/hold時間、
  baudの物理タイミング（host coreも同方針）
- **ビットバン波形のデコード**: edge列からのプロトコル復元。仮想時計上でも
  意味のある解析にならない（所有者方針、X24メモ）
- **アナログ回路網の模擬**: 分圧・フィルタ・ADC非線形性。channelで「値」を
  注入するだけで、値がどう作られるかは扱わない
- **シリコンのエラッタ・個体差の再現**
- **実時間の並行性race**: FreeRTOS/`esp_timer`は対象外（計画で既定）。
  決定的なtick/advanceToの因果だけを扱う

この節が「線を越えた要望」への既定の答えになる: 越える要望は、実機テスト、
専用シミュレータ、または純関数の単体テストへ振り分ける。

## 4. 契約の要点（ヘッダ本文が正、ここは索引）

| 契約 | 決定 | 検証 |
| --- | --- | --- |
| 再入 | 環境はDeviceを再入しない。Device内からの`HostPort`呼び出しが引き起こす効果（ISR等）は即時記録・**Device呼び出し完了後に配送** | X32（即時配送ならdepth 2、延期でdepth 1） |
| 時間 | `advanceTo`のnowUsは`reset()`間で単調非減少、同値の再呼び出し可（二重発火禁止）、飛びは全ての期限到来分をその呼び出しで期限順に処理、`reset()`は保留期限を破棄、callback中の`nowMicros()`は最後の`advanceTo`以上 | X33 |
| I2C | `I2cTransfer{stop, continued}`とArduino準拠の`I2cStatus`（0〜4） | X30 |
| channel | `channelWrite`は全量適用時のみtrue（falseは環境が診断）。`channelRead`はsnprintf型（必要長を返し、cap分だけ書く） | X33 |
| dump | snprintf型（必要長を返し、cap>0なら常にNUL終端） | X33 |
| serialOut | 任意byte（NUL含む）を運ぶ。表示側の課題でIFの課題ではない | — |
| 借用バッファ | 引数のポインタはcall中のみ有効 | ヘッダ |

## 5. 未決（IF最終決定時に締める）

1. frame経路にも要求/応答の対応付け（`re=`）が要るユースケースが出るか
   （現状の実例は全て一方向+遅延応答で足りている）
2. registry容量の既定値と、schema指紋の生成規約（手書き定数か、レイアウト
   からの派生か）
3. 集約記録をtransaction以外の塊（`writeBytes`、frame連送）へ広げる基準と
   checksumの種類（X28の未決）
4. `maxFrameBits`のbusごとの差を実際に持つ環境が現れた場合の、format側の
   分割規則の書き方（X27の`acme.bulk.1`が1例目）
