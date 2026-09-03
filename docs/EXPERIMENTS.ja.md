# 実験台帳

内部の記録。日本語のみ。公開仕様を決める前に、host上で観測した事実と
試した候補を数値で残す。

ここにある現在の実験は、開発計画を作る前に行ったhost coreの準備計測である。
実験が通ったことを仕様の承認とはみなさない。以後の順序と承認条件は
[DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md)に従う。

## 記録の読み方

- **事実**: host-arduino-core 1.7.0の実行結果。EmbedBenchの仕様ではない
- **候補**: 実験内だけに書いた方式。採用前であり、公開ヘッダには入れない
- **未決**: 実験結果を踏まえて別途承認する事項

すべて `lang-ship:host:host`、`socket://localhost`、実機なしで実行する。

## X0. 最小経路

対象: `tests/smoke/`

| 観測 | 値 |
| --- | ---: |
| ライブラリの解決 | `EmbedBench 0.0.0` |
| `delay(3)` の仮想経過 | 3,000 us |
| wait hook呼び出し | 3回 |
| preSetup / postSetup / preLoop / postLoop | 1 / 1 / 2 / 1 |

**事実:** Arduinoライブラリ → host core → socket → pytestの最小経路は成立する。

## X1. 時計口の粒度

対象: `tests/clock/`

| 呼び出し | 仮想経過 | wait回数 | waitへ渡された単位 |
| --- | ---: | ---: | --- |
| `delay(3)` | 3,000 us | 3 | 1,000 us × 3 |
| `delayMicroseconds(2500)` | 2,500 us | 1 | 2,500 us × 1 |
| `delay(0)` | 0 us | 0 | なし |
| `delayMicroseconds(0)` + `yield()` | 0 us | 2 | 0 us × 2 |
| 空のUARTをtimeout 4msで1byte読む | 4,000 us | 4 | 1,000 us × 4 |

**事実:** host coreのwait呼び出し1回をtick 1回とみなすことはできない。
特に `delayMicroseconds` は引数全体が1回で渡される。

**未決:** 0 us waitを「外部処理の機会」とするか、時間が進まないため何もしないか。
後者だけでは `yield()` を含むビジーウェイトへ入力を届けられない。

## X2. ライフサイクル順序

対象: `tests/lifecycle/`

記号は A=preSetup、S=setup本体、B=postSetup、C=preLoop、L=loop本体、
D=postLoop。2回目のloop本体で観測した列は次のとおり。

```text
ASBCLDCL
```

この時点で完了済みloop数は1。したがって `loopCount()` はpostLoopで増える。

## X3. 周辺機能ポート

対象: `tests/ports/`

| 経路 | 入力 | 観測結果 |
| --- | --- | --- |
| GPIO write | HIGH → LOW | hook 2件 |
| GPIO注入 | held HIGH | read結果 1 |
| GPIO read hook | HIGHを反転 | read結果 0 |
| Analog注入 | raw 1234 / 3300mV | 1234 / 3300 |
| Analog read hook | rawを1/2 | 617 |
| SPI | 0xA5、8MHz、mode 0 | 応答0x5A、転送1件、transaction edge 2件 |
| Wire write | addr 0x34、AB CD | status 0、write hook 1件 |
| Wire read | addr 0x34、2byte | 11 22、read hook 1件 |
| UART | `AT` → `OK` | wait 1回、仮想1,000usで応答 |

**事実:** 観測と応答を同じhost hookで行える。ただしhook自体には観測者と
応答者の役割分離はない。

**付随結果:** `Wire.requestFrom(0x34, size_t(2), true)` は複数overloadのため曖昧。
アドレスも `uint16_t` と明示するとコンパイルできた。将来の公開APIでは、整数
リテラルで曖昧になるoverload集合を避ける。

## X4. 固定tick境界への分割候補

対象: `tests/tick_split/`

1,000us tickの候補実装を実験内だけに置いた。

| 操作後 | 現在時刻 | 発火済みtick | 次の境界 |
| --- | ---: | ---: | ---: |
| 2,500us進める | 2,500 | 2 | 3,000 |
| 499us進める | 2,999 | 2 | 3,000 |
| 1us進める | 3,000 | 3 | 4,000 |
| 0usの`yield()` | 3,000 | 3 | 4,000 |
| 続けて`delay(3)` | 6,000 | 6 | 7,000 |

**候補として確認できたこと:** waitの長さによらず、絶対時刻上の境界で分割すれば
端数を次回へ持ち越し、tickの取りこぼしと二重発火を避けられる。

## X5. host hookの登録枠

対象: `tests/hook_slots/`

| hook | A登録後にB登録 | 結果 |
| --- | --- | --- |
| GPIO write | Aで1操作、Bへ交換して1操作 | A=1件、B=1件 |
| clock | A登録後、Bへ交換して7us待つ | A=0件、B=1件 / 7us |
| lifecycle | main前にA、続けてBを登録 | A=0件、B=3件（最初のloop本体まで） |

**事実:** 新しい登録は古い登録を置き換える。複数の利用者がhost coreへ直接登録する
構成にはできない。

**未決:** EmbedBenchが各host hookを1枠だけ占有し、その上で観測者・応答者を
多重化する責務を持つか。現時点では必要性だけが確認でき、APIや上限は未定。

## X6. host拡張点の不足

対象: `tests/host_gaps/`

| 操作 | 実測 |
| --- | ---: |
| GPIOをLOW/HIGHへ変更 | edge 2、ISR callback 0回 |
| `analogRead`後のread hook回数 | 1回 |
| 続けて`analogReadMilliVolts`後のread hook回数 | 1回のまま |
| UART TX | 2byteをqueueへ保存、2byteを後からdrain可能、同期activity hookなし |

不足する能力と依頼候補は[HOST_EXTENSION_AUDIT.ja.md](HOST_EXTENSION_AUDIT.ja.md)へ分離した。

## X7. 0 us waitの再入と無限ループ条件

対象: `tests/zero_wait/`

`yield()` によるビジーウェイト中、wait hookが唯一の外部処理の機会になる。
hook内で5回目の0 us waitのときにGPIO注入で解放する構成で計測した。

| 観測 | 値 |
| --- | ---: |
| 解放までのspin回数 | 5 |
| 0 us wait回数 | 5 |
| hook内から `yield()` した際の再入depth | 2 |
| 再入guardで弾いた回数 | 1 |
| 全期間の仮想経過 | 0 us |
| 注入なしでspin 50回した際の0 us wait回数 | 50（時刻は不変） |
| `delay(0)` を5回した際のwait呼び出し | 0回 |

**事実:**

- 外部処理がhook内から待ちAPI（`yield()` 含む）を呼ぶとwait hookへ再入する。
  depth guardがなければ外部処理が二重実行される
- 0 us waitでは仮想時刻が進まないため、注入が起きない限りビジーウェイトは
  永遠に回る。検出手段は0 us wait回数の計数しかない
- `delay(0)` はwait hookに一切届かない。`delay(0)` でspinするアプリには
  外部処理の機会を差し込めない（`yield()`・`delayMicroseconds(0)` は届く）

**未決:** 0 us waitの連続回数に上限を設けて診断イベントにするか。

## X8. tick内から別の待ちAPIを呼んだ場合の3方式比較

対象: `tests/tick_guard/`

1,000 us tickの進行役（tick callback）が、tick 2の中で別の待ちAPIを呼ぶ。
アプリ側は `delay(5)`。3方式を実験内だけの候補として比較した。

| 方式 | 入れ子API | tick数 | director最大depth | 遅延発火 | 拒否回数 | 時刻逆行 | 最終時刻 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 許可（再入分割） | `delay(2)` | 5 | 2 | 0 | 0 | 0 | 5,000 |
| 時間は進め、tickは延期 | `delay(2)` | 5 | 1 | 2 | 0 | 0 | 5,000 |
| 拒否（時刻凍結） | `delay(2)` | 5 | 1 | 0 | 51（cap発動2回） | 1 | 5,000 |
| 拒否 | `delayMicroseconds(500)` | 5 | 1 | 0 | 1 | 0 | 5,000 |
| 拒否 | `yield()` | 5 | 1 | 0 | 0（0 us検出1回） | 0 | 5,000 |

tick発火時刻の列:

```text
許可:   1@1000 2@2000 3@3000 4@4000 5@5000   ただしtick 3,4はtick 2の内側で発火
延期:   1@1000 2@2000 3@4000 4@4000 5@5000   tick 3,4は境界より遅れて発火
拒否:   1@1000 2@2000 3@3000 4@4000 5@5000   cap脱出後に時刻が4000→3000へ逆行1回
```

**事実:**

- host coreの `delay()` は「`clockNowMicros()` がdeadlineへ達するまで
  `clockWaitMicros(1000)` を繰り返す」ため、**時刻を凍結する拒否は `delay()` を
  永遠に回す**。cap付き脱出（50回で強制進行）を入れない限り実験自体が終わらない
- 脱出で時間だけ進めると、未発火境界が現在時刻より過去になり時刻逆行が起きる
- 単発呼び出しの `delayMicroseconds()` に限れば、時刻凍結の拒否は1回で
  きれいに戻る（ただし呼び出し側は時間が進まなかったことを知れない）
- tick内 `yield()` は0 us waitとして届き、無視しても進行に影響しない

**候補として確認できたこと:** 「時間は進め、tick発火だけ延期」が唯一、
depth 1・時刻単調・アプリのdelay完走をすべて満たす。ただしtick 3,4が
境界時刻より遅れて発火したことをイベントに残す必要がある。

**未決:** 進行役内の待ちAPIを、延期で許容するか、診断イベント付きで
即時失敗（イベント記録＋停止）にするか。

## X9. 単一hookから複数listenerへの配送

対象: `tests/listener_fanout/`

pin write hookを実験内のdispatcherが1枠所有し、固定4 slotへ配送した。

| 操作 | 結果 |
| --- | --- |
| listener 0件で発火 | 中央カウントのみ増加、配送0件 |
| 4件登録して発火 | 登録順に4件配送（順序 A B C D） |
| 5件目の登録 | 失敗を返し、診断カウント1 |
| 通常解除後の発火 | 残り3件のみ、順序保存 |
| callback内で自分を解除 | 同一イベントの残りslotは発火、次イベントから消える |
| callback内で後方のlistenerを解除 | 解除されたlistenerは**当該イベントを受け取らない** |
| dispatcher状態サイズ | 72 byte（slot 4 × 16 + カウンタ2） |

**事実:** index走査の固定slot配列なら、callback中の解除でも安全に配送を
続けられる。ただし「後方解除は当該イベントに影響する」「前方解除は影響しない」
という位置依存が残る。

**未決:** callback中の解除を即時反映にするか、イベント配送完了まで遅延させて
位置依存をなくすか。登録上限4で足りるかはデバイス数の実例が出てから決める。

## X10. イベント記録のサイズ・満杯方式・1行形式

対象: `tests/event_buffer/`

seq(4) + time(8) + ctx/origin(2) + kind(2) + len(2) + payload(8) の候補recordで計測。

| 観測 | 値 |
| --- | ---: |
| 1イベントのメモリサイズ | 32 byte（alignment込み） |
| 8 slot固定バッファ | 256 byte |

12イベントを8 slotへ投入した満杯方式の比較:

| 方式 | 保持seq | 損失 | 欠落の検出方法 |
| --- | --- | ---: | --- |
| 新規を捨てる | 1〜8 | 4 | dropped計数 + 最終提示seq(12) > 最終保持seq(8) |
| 古い方を上書き | 5〜12 | 4 | overwritten計数 + 先頭提示seq(1) < 先頭保持seq(5) |

どちらも計数と保持seq範囲だけで欠落を検出でき、黙った成功にはならない。

同一イベントの1行直列化3候補（gpio.write pin=5 level=1, seq=42, t=123456）:

| 候補 | gpio.write | i2c.write 2byte | 100,000行の生成時間* |
| --- | ---: | ---: | ---: |
| sequence先頭・固定幅 | 59 byte | 68 byte | 約37 ms |
| timestamp先頭・固定幅 | 59 byte | 68 byte | 約44 ms |
| JSON Lines | 94 byte | 109 byte | 約39 ms |

*実時間のためこの値のみ参考値。生成byte数は決定的（59/94 byte × 100,000）。

**事実:** JSONはfixed-width textの約1.6倍の容量。生成時間は3候補で大差ない。
容量差の主因はフィールド名の繰り返しであり、行数が増えるほど差は容量に直結する。

**未決:** goldenの既定形式。parse時間・diff読みやすさの比較（WP-B2の残り）。

## X11. Wireの観測者と応答デバイスの分離

対象: `tests/wire_split/`

Wireのwrite/read hookを実験内のdispatcherが1枠所有し、副作用なしの観測者2つと、
アドレスごとに最大1つの応答デバイスへ分離した。

| 操作 | 結果 |
| --- | --- |
| 同一アドレスへの2つ目のbind | 拒否、診断カウント1、以後も1つ目が応答 |
| bind済み0x34へwrite AB CD | status 0（デバイスが決定）、観測者2つとも1件観測 |
| 0x34から2byte read | デバイス応答 AC CE、観測者2つとも観測 |
| 未boundの0x55へwrite | status 2（dispatcherが決定）、診断1、観測者は観測 |
| 未boundの0x55から read | 0 byte、診断1、観測者は観測 |

**事実:** 「アドレスごとに応答者は最大1つ、観測者の通知経路に戻り値なし」という
構造なら、statusとreadデータの出どころは常に一意になる。未bound時の既定status
（2 = NACK on address）はdispatcher層の責務として決められる。

**未決:** 未bound writeの既定statusを2にするか、診断イベント＋停止にするか。
observerへ渡す情報（payload全体か、長さだけか）。

## X12. 直接呼び出し3案の比較（WP-A2）

対象: `tests/device_route/`

同一のregister-map温度センサ模型で、同一シナリオを3案で実行した。
シナリオ: 仕込みで温度raw=250 → 走行中にappがI2Cでconfig=5を書き、進行役が
温度を300へ変更し、appがI2Cで温度を読む → 検分でconfigを読む。

イベント記号: w=I2C write観測、r=I2C read観測、s=仕込みchannel、
j=走行中注入channel、i=検分channel。

| 測定 | 案1 全直接 | 案2 全共通IF | 案3 仕込み/実行/検分分離 |
| --- | ---: | ---: | ---: |
| 最終状態（温度 / config） | 300 / 5 | 300 / 5 | 300 / 5 |
| 記録イベント数 | 3 | 6 | 4 |
| イベント列 | wwr | swjwri | wjwr |
| 走行中のログ欠落 | **1** | 0 | 0 |
| channel callback数 | 0 | 3 | 1 |
| 型変換数 | 0 | 6 | 2 |
| 利用側行数 | 10 | 15 | 12 |
| device側行数 | 6 | 20 | 26 |

device側行数の内訳: 型付きAPI 6行、channel adapter 20行。案1は型付きAPIのみ、
案2はadapterのみ、案3は両方を持つ。

**事実:**

- 案1は走行中の注入がイベント列に一切現れない（欠落1）。ログから因果を
  再構成できず、欠落したこと自体も検出できない
- 案2は欠落0だが、仕込みと検分もイベントになり、goldenが走行と無関係な行で
  汚れる（6行中2行が非走行由来）。型変換も最多
- 案3は欠落0のまま、イベント列の全行が走行中の因果になる。追加コストは
  注入1回あたり型変換2回と、利用側+2行

**候補として確認できたこと:** 計画4.2の「仕込み=直接、走行中=channel経由、
検分=直接」の3分割は、ログ欠落0とgoldenの純度を同時に満たす。計画4.3の
案A（EmbedBench経路で記録してから配送）を走行中の外部作用だけに限定する
方針と数値が整合する。

**未決:** 検分結果を証拠として残したい場合のEmbedBench経由dump経路の形。
device側行数は案3が最大（26行）になるため、adapter記述を減らす共通部品を
Gate Cで検討する。

## 次に必要な実験

1. X8の「延期」方式で、遅延発火したtickへ付けるtimestampの表現（境界時刻か発火時刻か）
2. listener解除の位置依存（X9）をなくす遅延反映方式の比較
3. 1行形式のparse時間とdiff差分行数の比較（WP-B2の残り）
4. UART/SPIにもX11の観測者・応答者分離を適用して同じ核が使えるかの確認
5. 検分を証拠に残す場合のEmbedBench経由dump経路の比較（X12の未決）
