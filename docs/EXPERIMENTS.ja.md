# 実験台帳

内部の記録。日本語のみ。公開仕様を決める前に、host上で観測した事実と
試した候補を数値で残す。

ここにある現在の実験は、開発計画を作る前に行ったhost coreの準備計測である。
実験が通ったことを仕様の承認とはみなさない。以後の順序と承認条件は
[DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md)に従う。

## 記録の読み方

- **事実**: host-arduino-coreの実行結果。EmbedBenchの仕様ではない
- **候補**: 実験内だけに書いた方式。採用前であり、公開ヘッダには入れない
- **未決**: 実験結果を踏まえて別途承認する事項

すべて `lang-ship:host:host`、`socket://localhost`、実機なしで実行する。

**host coreのversion:** X0〜X12は1.7.0で計測した。2026-09-03に全実験のpinを
1.7.1へ更新し、X0〜X12が同一結果のまま通ることを再確認した（1.7.1は追加のみで
既存挙動を変えないという主張どおり）。X13以降は1.7.1が対象。

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

**追記:** ここで確認した不足はhost core 1.7.1で全て解決した。検証はX13〜X15。

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

## X13. 割り込み口の検証（H1、host core 1.7.1）

対象: `tests/interrupt_port/`

記号: A=attach、D=detach、E*n*=enter(depth n)、X*n*=exit(減算後depth)、w=pin write。

| 操作 | 結果 |
| --- | --- |
| `attachInterrupt(27, h, RISING)` | attachイベント1件、**生mode=3**、正規化trigger=1(kTriggerRising) |
| `setPinValue` / `digitalWrite` で線を動かす | fires=0（coreは自発的に発火しない） |
| `triggerInterrupt(27)`、handler内で`digitalWrite` | `E1wX0`。ISR内のバス通信がenter/exitに挟まれ文脈識別できる |
| handlerが自分を再trigger | `E1E2X1X0`、fires=2、最大depth=2 |
| handlerが自分をdetach | `E1DX0`、以後のtriggerはfalse |
| `attachInterruptArg` | arg経由でcallback到達 |
| 再attach（mode変更CHANGE） | firesは2のまま保持、**生mode=1**、trigger=3(kTriggerChange)、detachイベントなしの置換 |
| 未登録pinのtrigger / detach | false / イベントなし |

**事実:**

- H1で依頼した分担（保持と呼出し口のみ、edge判定なし）がそのまま提供された
- **生mode定数はarduino-esp32と不一致**（RISING: host 3 / 実機 1、CHANGE: host 1 / 実機 3）。
  数値照合は静かに誤一致するため、照合は正規化`InterruptTrigger`のみとする
- enter/exitの括りにより、EmbedBenchはISR由来のイベントへ `ctx=isr` を機械的に付与できる

## X14. UART activity hookの検証（H2、host core 1.7.1）

対象: `tests/uart_activity/`

記号: B=begin、N=end、C=config、T*n*=TX n byte、R=RX 1byte消費、F*n*=flush破棄 n byte、w=pin write。

| 操作 | 結果 |
| --- | --- |
| `begin(9600)` / `updateBaudRate` / `end()` | B / C / N 各1件、`uartNum()`=1 |
| `digitalWrite` → `print("AT")` → `digitalWrite` | trace `wT2w`。**TXがGPIOイベント間の正しい位置に残る**（X3/X6のpollingでは不可能だった） |
| `kUartTx` callback内から `pushRx("OK")` | `readBytes`が**wait 0回**で"OK"を受信（X3の同一交換は1 wait / 1,000us） |
| 2byteの消費 | `RR`（1byteごとに1イベント） |
| hook使用中のtx queue | 2byte残存（hookとpollingは共存、両方使うと二重観測） |
| テスト側の`readTx` | イベントなし（線のこちら側の行為は通知されない） |
| `pushRx("junk")`後の`flush()` | `F4`（未読破棄も欠落なく報告） |

**事実:** H2で依頼した全性質（TXは`write()`前、callbackからの`pushRx`安全、
port番号と受理byte数、begin/end/config通知）が提供された。
EmbedBench側はhook一本化とし`readTx`を使わない（二重観測の回避）。

## X15. Analog mV読取り観測の検証（H3、host core 1.7.1）

対象: `tests/analog_mv/`

| 操作 | 結果 |
| --- | --- |
| hookなしで`analogReadMilliVolts` | 注入値3300がそのまま返る |
| `setAnalogMilliVoltsHook`（held/2を返す） | 結果1650、held=3300を受領、mv hook 1回、raw hook 0回 |
| `setAnalogReadHook`後の`analogRead` | raw hook 1回、mv hookは増えない（完全に独立） |
| `analogReadResolution(9)` → `analogSetWidth(11)` | config hookへ bits=9, 11 の順で2回（両綴りは区別なし） |
| `clearAnalogHooks()` | 4本すべて解除。mv読取りは3300へ戻り、config通知も止まる |

**事実:** H3の依頼どおり、mV読取りの観測・差し替えが可能になり、既存raw hookは
無変更。X6の観測の穴は塞がれた。

## X16. 割り込みの縦切り（host core 1.7.1）

対象: `tests/interrupt_slice/`

進行役の線注入 → edge判定 → イベント記録 → `triggerInterrupt` → ISR内バス通信の
文脈付与を、1本のイベント列で通した。edge判定は正規化`InterruptTrigger`のみで行う
（X13の規則）。観測した列（seq / ctx / origin / 内容）:

```text
01 main app gpio.write pin=4 val=1
02 main dir inject pin=27 0->1 match=1
03 isr core isr.enter pin=27
04 isr app gpio.write pin=5 val=1
05 isr core isr.exit pin=27
06 main dir inject pin=27 1->0 match=0
07 main app gpio.write pin=4 val=0
```

**事実:**

- 注入 → 記録 → 反映 → 判定 → ISRの順序を候補coreの1か所で管理できる
- ISRが行ったバス操作（seq 04）はenter/exitの深さから機械的に `ctx=isr` になる
- 不一致edge（seq 06、FALLINGにRISING登録）は注入イベントだけ残りISRは走らない。
  fires=1のままで、「登録があるのに発火しなかった」ことがログから判別できる
- `setPinValue` はpin write hookを呼ばないため、注入がgpio.writeとして二重記録されない

## X17. UART AT会話のgolden（host core 1.7.1）

対象: `tests/uart_golden/`

`kUartTx` 内の即時応答と、進行役がtick境界で配送する遅延応答を、仮想timestampつきの
1本のイベント列にした。デバイス模型: "AT"は即時に"OK"、それ以外のATコマンドは
1 tick（1,000us）後に"OK"。

```text
01 000000 uart.begin
02 000000 uart.tx AT
03 000000 uart.rx O
04 000000 uart.rx K
05 002000 uart.tx AT+S
06 003000 dir.inject OK
07 003000 uart.rx O
08 003000 uart.rx K
```

| 交換 | 応答方式 | アプリの読取り経過 | wait回数 |
| --- | --- | ---: | ---: |
| "AT" → "OK" | `kUartTx` 内で即時 `pushRx` | 0 us | 0 |
| "AT+S" → "OK" | tick境界で進行役が配送 | 1,000 us | 1 |

**事実:** 応答遅延をデバイス模型の性質としてtick単位で表現でき、TX・注入・RX消費の
全てが同じ仮想時間軸のイベント列に正しい順序で残る。`readBytes` のwait中に
進行役が `pushRx` する経路（tick配送）は安全に動く。

**注記（レビュー指摘）:** この実験の即時応答は `kUartTx` callback内から直接
`pushRx` しており、devの送信自体は注入イベントとして残らない（seq 02と03の間に
devの応答行が無い）。実験上の簡略であり、候補coreではdev応答もcoreのUART RX
sink経由で記録してから `pushRx` する（監査の利用規則5）。sink経由版は**X21で
実施済み**で、dev送信が独立イベントとして正しい位置に残ることを確認した。

## X18. I2Cの縦切り（WP-C1の先行実証、host core 1.7.1）

対象: `tests/i2c_slice/`

無改造のArduinoアプリ（Wire APIのみ）を、候補coreがWire hookと時計hookで下から
駆動した。仕込み（温度raw=250、直接・非記録）→ 走行（configをI2C書込み、温度を
I2C読取り、`delay(3)`中のtick 2で進行役が温度300をchannel注入、再読取り）→
検分dump。250=0x00FA、300=0x012C。

```text
01 000000 main app i2c.write addr=48 data=0105
02 000000 main app i2c.write addr=48 data=00
03 000000 main app i2c.read addr=48 data=00FA
04 002000 tick dir chan.write chan=0 data=012C
05 003000 main app i2c.write addr=48 data=00
06 003000 main app i2c.read addr=48 data=012C
07 003000 main dir dump temp=012C cfg=05
```

| WP-C1完了条件 | 結果 |
| --- | --- |
| アプリ無改造 | app部はArduino APIのみ（before=250 / after=300を正しく観測） |
| イベント欠落0 | 注入mutation数 − 記録注入イベント数 = 0。走行中の外部作用は全7イベントに記録 |
| 同一結果の再現 | 3回実行してtraceがbyte単位で一致（run2_same=1 run3_same=1） |

**事実:** アプリ → Arduino API → host hook → 候補core → ログ → デバイス模型 →
応答、テスト → 注入、の縦切り全体が決定的に成立する。timestampは仮想時計から、
文脈はtick進行から機械的に決まる。

**注記:** Gate A/Bは未承認のため、これは仕様決定ではなくWP-C1の候補実証である。
イベント1行の綴りもX10の候補1を仮置きしたに過ぎない。

## X19. 拒否・破棄された操作の現hookでの見え方

対象: `tests/reject_paths/`

Gate Aの「ログ完全性の範囲」を決めるための実測。hostが受理せず捨てる呼び出しが
現在のhookでどう見えるかを数値化した。

| 操作 | 戻り値・状態 | hookイベント |
| --- | --- | ---: |
| `beginTransmission`なしの`endTransmission` | status 4 | 0 |
| Wire送信バッファoverflow（200byte書いて128byte受理） | status 1 | 0（模型へ届かない） |
| UART TX overflow（1,200byte書いて1,024byte受理） | `txOverflowed()`=1（sticky） | `kUartTx` 11件 / 1,024byteのみ |
| 未attach pinへの`ledcWrite` | false | 0 |
| 周波数0の`ledcAttach` | false | 0 |
| 20bit超分解能の`ledcAttach` | false | 0 |
| （対照）正当な`ledcAttach` | true | 1 |

**事実:**

- hostが拒否・破棄した操作は、**どの経路でもhookに一切届かない**。EmbedBenchは
  アプリがそのAPIを呼んだこと自体を知れない
- 検出手段はアプリへの戻り値（status 4/1、false）とsticky flagだけで、
  「いつ、何byte失ったか」をイベント順序へ戻す方法は存在しない
- したがってGate Aは、(a) hostへreject/overflow通知を依頼して拒否も完全性の
  対象にする（監査H4案）か、(b) 完全性を「hostが受理した外部作用」に限定するか
  の二択になる。coreの引数検査で肩代わりする案は、hookに届かない以上成立しない

## X20. イベント完成タイミング4方式の比較（EVENT_MATRIX未決1）

対象: `tests/event_timing/`

応答を伴う操作（I2C read）の最中に、デバイス模型が自分のIRQ線をcoreのGPIO sinkで
上げる——という再入シナリオで、イベント完成の4方式を比較した。log列は保持順
（=sequence順）、streamは完成（外部sinkへ流れる）順。

| 方式 | 行数/操作 | stream順 | seq順=stream順 | 未完成slotの露出 | devのIRQ線 | 再入の因果 |
| --- | ---: | --- | :-: | ---: | --- | --- |
| (a) 要求/応答2行 | 2 | 123 | ○ | 0 | 即時反映 | **req(1)とresp(3)の間にIRQ(2)が挟まり可視** |
| (b) 受付時予約・応答後完成 | 1 | **21** | × | 1 | 即時反映 | IRQ(2)がop(1)の後に見え、「操作中」か「操作後」か判別不能 |
| (c) callback中のsink禁止 | 1+diag | 12 | ○ | 0 | **消失**（rejected=1、pin動かず） | 再入自体が起きない |
| (c') callback中は延期 | 2 | 12 | ○ | 0 | 遅延反映（callback内の読み戻しはstale） | IRQ(2)がop(1)の後。実際より遅く適用 |

各方式のlog:

```text
(a)  1 i2c.req addr=48 req=1 / 2 gpio.inject pin=7 val=1 / 3 i2c.resp addr=48 data=2C
(b)  1 i2c.read addr=48 data=2C / 2 gpio.inject pin=7 val=1   ※完成順は2→1
(c)  1 i2c.read addr=48 data=2C / 2 diag.reject sink_in_response
(c') 1 i2c.read addr=48 data=2C / 2 gpio.inject pin=7 val=1 deferred=1
```

**事実:**

- 再入の因果（IRQが読取り応答の**中で**起きたこと）をログから復元できるのは
  (a)だけ。代償は応答つき操作1回あたり2行と、req/respの対応付け
- (b)はslot予約でsequence順の保持はできるが、完成順が逆転するため
  逐次外部sinkへ流す運用ができず、途中停止時に未完成イベントの穴が残る
- (c)は順序が最も単純だが、反応的なデバイス模型（IRQ・DRDY）が成立しない
- (c')は機能と順序を両立するが、デバイスがcallback内で自分の線を読み戻すと
  古い値を見る（pin_cb=0）という意味論の歪みが残る

## X21. UART応答のsink経由記録（X17改、レビュー指摘4の解消）

対象: `tests/uart_sink/`

X17と同じAT会話を、dev応答をcoreのUART RX sinkで**記録してから** `pushRx` する
構成でやり直した。X17で欠けていた「devがいつ送信したか」の行が入る。

```text
01 000000 uart.begin
02 000000 uart.tx AT
03 000000 dev.tx OK        ← X17には無かった行。appのTXとRX消費の間に入る
04 000000 uart.rx O
05 000000 uart.rx K
06 002000 uart.tx AT+S
07 003000 dev.tx OK        ← tick配送でも同じsinkを通るため同形式で残る
08 003000 uart.rx O
09 003000 uart.rx K
```

**事実:** sink経由にしても即時性は保たれ（ex1はwait 0回・経過0us、ex2は
1 tick遅延の1,000us）、即時応答とtick遅延応答が同じ `dev.tx` イベントとして
正しい時刻・位置に残る。`kUartTx` callback内からsinkを呼ぶ経路（記録→`pushRx`）
は安全に動く。

## X22. 統合draft coreの複数バス同時計測

対象: `tests/core_draft/`、実装は `src/embedbench_draft.{h,cpp}`

**工程上の位置づけ:** 所有者の判断で、Gate承認を後置して「実装しながら数値を
取り、最後に決定する」フェーズに入った（2026-09-03）。各実験の最優秀候補
（X4/X7/X8のtick処理、X11の観測者/応答者分離、X16のedge判定と`ctx=isr`、
X20勝者の要求/応答2行分割、X21のsink記録）を1つのdraft coreに統合し、
`src/embedbench_draft.{h,cpp}` に**未承認・手戻り前提のdraft**として隔離した。
計画7項の「Gate未承認の型をsrc/へ追加しない」の例外であり、公開APIの確定ではない。

シナリオ（アプリは無改造のArduinoコード）: I2Cでconfig書込み →
`attachInterrupt`してDRDY待ちのビジーウェイト（`yield()`）→ 進行役が3回目の
0 us waitでDRDY線を注入 → ISRがGPIO書込み → I2Cで温度読取り(250) →
UARTで"AT"送信、devがsink経由で"OK"応答 → `delay(2)`中のtick 2で進行役が
温度300をchannel注入 → 再読取り(300) → dump。

観測されたイベント列（21行、3回実行してbyte一致）:

```text
01 000000 main app i2c.req addr=48 data=0105
02 000000 main dev i2c.resp status=0 re=1
03 000000 main app int.attach pin=27 trig=1
04 000000 tick dir gpio.inject pin=27 0->1 match=1
05 000000 isr core isr.enter pin=27
06 000000 isr app gpio.write pin=5 val=1
07 000000 isr core isr.exit pin=27
08 000000 main app i2c.req addr=48 data=00
09 000000 main dev i2c.resp status=0 re=8
10 000000 main app i2c.rd.req addr=48 req=2
11 000000 main dev i2c.rd.resp len=2 data=00FA re=10
12 000000 main app uart.tx AT
13 000000 main dev dev.tx OK
14 000000 main app uart.rx O
15 000000 main app uart.rx K
16 002000 tick dir chan.write chan=0 data=012C
17 002000 main app i2c.req addr=48 data=00
18 002000 main dev i2c.resp status=0 re=17
19 002000 main app i2c.rd.req addr=48 req=2
20 002000 main dev i2c.rd.resp len=2 data=012C re=19
21 002000 main dir dump temp=012C cfg=05
```

| 測定 | 値 |
| --- | ---: |
| イベント数（うち応答行） | 21（5） |
| 3つの文脈（main / tick / isr）の共存 | 順序保存で成立 |
| dropped / 診断 / late tick | 0 / 0 / 0 |
| 1イベントのメモリ（text 44byte内包） | 72 byte |
| 応答行の対応付け | `re=<要求seq>` 1フィールド |
| draft core規模 | ヘッダ83行 + 実装457行 |
| 実験側の記述 | 182行（模型・binding・シナリオ込み） |

**事実:**

- GPIO・割り込み・I2C・UART・仮想時間が**1本のsequence軸**に載り、ISR内の
  バス書込み（seq 06）、要求と応答の間のイベント、tick注入がすべて正しい
  位置に残る
- 2行分割（X20勝者）の実コスト: この現実的なシナリオで21行中5行が応答行。
  対応付けは `re=` 1フィールドで足りた
- 応答callbackなしの読取り（held値のGPIO read）は1行の完成イベントにする
  折衷が成立する（X20の問題は応答callbackがある操作だけに存在するため）
- 0 us wait注入 → edge判定 → ISR → 通常フローへの復帰、という最難の経路が
  他のバスと混在しても順序・再現性を壊さない

**draft v1の未実装（次回りの手戻り候補）:** Analog、SPI、`Wire1`/`Serial2`、
lifecycle連動のrun window、listener多重化、バッファ満杯時の診断イベント化。

## X23. ポータブルなデバイスIF（固定対象の中心）

対象: `tests/device_if/`、IF本体は `src/embedbench_device.h`
（参照模型はX29で `tests/common_models/` へ移し、複数環境から同一ソースを共有する形にした）

**工程上の位置づけ:** 所有者の判断（2026-09-03）で、**確実に固める対象を
「デバイス模型とのIF」に絞った**。デバイスIFは純粋なC++11でプラットホーム差が
なく他環境へポータブル。その上の環境側（host hookの所有、時計、記録の実装）は
環境ごとに差が出るため、**プラットホーム別の実装例**という扱いにする。
`src/embedbench_draft.*` はhost-arduino-core向け実装例の位置づけになった。

IFの形（`embedbench_device.h`、58行）: デバイス = 決定的状態機械。
入力はbus ops（`i2cWrite`/`i2cRead`/`spiTransfer`/`serialIn`）、注入
（`channelWrite`/`channelRead`）、時間（`advanceTo(nowUs)`）。出力は戻り値と
`HostPort`（`nowMicros`/`lineOut`/`serialOut`）のみ。論理line番号・アドレス
非依存（配線とアドレスはbinding側の責務）。遅延・周期動作はplatform timerでなく
`advanceTo` で実装する。

**検証1 — ネイティブ単体（Arduino・host core一切なし）:**

- `g++ -std=c++11 -Wall -Wextra -Werror` で模型2種＋FakePortをビルド・実行
- `#include` はIFヘッダと標準ライブラリのみ（禁止includeはテストが機械検査）
- register-map型（温度センサ、49行）: config書込みstatus 0、channel注入で
  DRDY線がFakePortへ上がる、block read 00FA、dump一致
- command型（ATモデム、50行）: "AT"は即時"OK"（t=0）、"AT+S"は
  `advanceTo(999)` では出ず `advanceTo(1000)` で"OK"（純粋な時間駆動латency）

**検証2 — 同じソース無改変でhost環境（draft core経由）:**

adapter（`HostPort`→draft coreのsink変換 + binding、**32行**）だけで同じ模型が
X22型のシナリオを駆動した。18イベント、3回byte一致。

```text
04 000000 tick dir gpio.inject... の代わりに、今回はデバイス自身が反応する:
04 000000 tick dir chan.write chan=0 data=012C   ← 進行役の注入
05 000000 tick dev gpio.inject pin=27 0->1 match=1 ← 模型がlineOut(DRDY)で反応(origin=dev)
06-08     isr ...                                  ← ISR起動、ctx=isr
14 001000 tick dev dev.tx OK                       ← モデムのadvanceTo駆動の遅延応答
```

| 測定 | 値 |
| --- | ---: |
| IFヘッダ | 58行（仮想関数11個、型はstdint/stddefのみ） |
| 模型: register-map型 / command型 | 49行 / 50行 |
| **プラットホームglue（adapter）** | **32行** |
| ネイティブ検証と host検証で共有する模型ソース | 同一ファイル無改変 |
| host側イベント列 | 18行、応答行3、diag 0、3回byte一致 |

**事実:**

- デバイスIFは純粋C++11で成立し、模型はArduinoもhost coreも知らずに書ける
  （Gate Cの完了条件を満たす）。同じソースがネイティブとhostの両方で
  決定的に同じ挙動を示す
- 環境側の責務はadapter 32行に集約された。「環境側はプラットホーム別の
  実装例」という整理は定量的にも成立する
- デバイス起点の外部作用（DRDY線、遅延UART応答）が `HostPort` 経由で
  origin=devとして記録され、X20/X21の経路規則と両立する

**未決（IF固定前の残項目）:** `serialOut`のbyte列にNULを含む場合の扱い、
channel idの割り当て規約、`dump`の文字列規約（機械可読性）、SPI/複数line/
複数serialを持つ複合デバイスでの`HostPort`拡張の要否。

## X24. 複合デバイス（SPI＋入出力line＋時間）でのIF検証

対象: `tests/spi_device/`

X23未決の第一項「複合デバイスでIFが伸びずに済むか」の検証。SPIディスプレイ型
模型（DC入力線でcommand/dataを判別、refreshでbusy出力線を上げ、時間で下ろす）
を書いた結果、IFに欠けていたのは**デバイスの入力line**だけだった。

**IFへの追加: `Device::lineIn(line, level)` 1つ（ヘッダ58→62行、+4行）。**
`HostPort` は無変更。入力line idと出力line idは別空間とした。

| 検証 | 結果 |
| --- | --- |
| ネイティブ（g++ -std=c++11 -Werror、Arduinoなし） | command応答0x00、dataはchecksum応答（10, 30）、refreshでbusy上昇、`advanceTo(999)`では下りず`advanceTo(1000)`で下降 |
| host環境（同一ソース無改変、adapter 32行のまま） | 16イベント、3回byte一致 |
| draft core（実装例側）への追加 | SPI transfer binding＋pin書込みのdevice転送（`setPinWriteForward`） |

host側イベント列の要点:

```text
10 000000 main app spi.req mosi=FF
11 000000 main dev gpio.inject pin=26 0->1 match=0   ← busy線がreqとrespの間に挟まる
12 000000 main dev spi.resp miso=00 re=10             （X20の再入ケースが実デバイスで発生）
13 000000 main app gpio.read pin=26 val=1
14 001000 tick dev gpio.inject pin=26 1->0 match=0    ← 時間駆動のbusy解除
15 001000 main app gpio.read pin=26 val=0
```

**事実:**

- register-map型・command型・SPI複合型の3類型が同一IFに載り、必要だった
  IF拡張は入力line 1メソッドのみ。「確実に固める」対象としてIFは小さく安定
- SPI応答中のデバイスline変化（busy）が、X20勝者の2行分割によりreq/respの
  間の正しい位置に記録される——比較実験の想定ケースが実デバイスでそのまま発生
- SPIは1byteごとにreq/respの2行になる。塊転送（フレームバッファ等）では
  行数が爆発するため、大量転送の記録粒度はGate F（X10でも指摘）の宿題

**設計方針メモ（所有者、2026-09-03、後日検証）— 未対応プロトコルの扱い:**
ビットバンで送受信された波形・edge列を後から解析するのは基本的に無理。
本ライブラリが受け渡すのは**送受信前の論理ビット列＋フォーマット情報のペア**
までとする。Raspberry Pi PicoのPIOで送るビット列、赤外線リモコンの符号化前
ビット列などはこのペアで転送し、ビット列の解釈は別定義とする。物理層と
エンコード/デコードは単体でユニットテスト可能であり実機で試す領域。
本ライブラリの役割はWeb開発のモックに近く、「実際の物理通信」ではなく
**「何をしたいのか」の通知**を扱う。SPI等が論理byte列受け渡しなのも同じ理由。
（現IFの `spiTransfer`/`serialIn`/`serialOut`/`channelWrite` はこの方針と
整合。汎用frame経路——format id付きビット列——のIF化は後日の検証項目）

## X25. 汎用frame経路 — 未対応プロトコルの拡張機構

対象: `tests/frame_port/`

X24の方針メモを実証した。拡張口がなければ、専用portのないプロトコル
（PIO・赤外線・WS2812・sub-GHz RF等）はすべてPIN経由のビットバンに落ち、
波形は後から解析できない。そこで**format id付きの論理ビット列（符号化前）**を
運ぶframe経路をIFへ追加した。

**IFへの追加（ヘッダ62→72行、+10行）:**

- `HostPort::frameOut(format, data, bits)` — デバイス→アプリ側のframe放出
  （既定no-opの任意機能。frame無関係な環境・模型は何も書かない）
- `Device::frameIn(format, data, bits)` — アプリ側→デバイスのframe到着
- bitsは任意bit数（PIOの非byte境界に対応）、解釈はformat idを鍵に
  デバイス側の責務。エンコード/デコードと物理層は本ライブラリの外

アプリ側は、実機ではエンコード・送信するprotocolドライバのhost版shimが
frameをそのまま環境へ渡す（実装例では `ebd::frameTx` / `setFrameReceiver`）。

模型: 無線ノード（アドレス0x04、command frame受信 → 自分宛のみ反応 →
1,000us後にtelemetry frameで応答。他人宛は無視）。

| 検証 | 結果 |
| --- | --- |
| ネイティブ（g++単体） | 他人宛frame無反応、自分宛は`advanceTo(999)`で出ず1000で応答、fmt=2/16bit/0401 |
| host環境（同一ソース無改変） | 4イベント、3回byte一致 |

```text
01 000000 main app frame.tx fmt=1 bits=16 data=0508   ← 他人宛（記録は残り、devは無視）
02 000000 main app frame.tx fmt=1 bits=16 data=0408
03 001000 tick dev dev.frame fmt=2 bits=16 data=0401  ← 遅延応答、devの送信時刻が残る
04 002000 main dir dump node power=1 pending=0
```

**事実:**

- 専用portのないプロトコルでも、ビットバンに落ちずに「アプリが何を送ろうと
  したか（符号化前の論理bits）」と「devが何を返したか」がformat id付きで
  イベント列に残る
- frameの解釈（アドレス照合・command判定）はデバイス側に閉じ、ログは
  中立なbits+formatのまま——「解釈は別定義」の分担が成立
- IF成長の履歴: X23=58行 → lineIn(+4) → frame(+10) = 72行。3類型+frame型の
  4種の模型でこの規模に収まっている

**未決:** format idの登録・衝突回避の規約（デバイスカタログ側の課題）、
1 frameの最大bit数、frame経路にも要求/応答の対応付け（`re=`）を入れるか。

## X26. format idの解決とframeのbus id

対象: `tests/format_registry/`（frame経路の改版はX25の`tests/frame_port/`にも反映済み）

X25未決の「format id登録規約」を検証した。所有者の指摘どおり、**未知の
プロトコル同士で重ならない固定番号は不可能**なので、4方式を比較した。

**1) 固定番号（X25の初版方式）— 衝突の実証（ネイティブ）:**
独立した2ライブラリが共にformat番号1を選ぶと、vendorの校正frame
{offset=4, gain=0} をnode模型が「アドレス4へpower off」と誤解釈し、
**検出手段なしに状態が反転した**（power 1→0）。X13のmode定数の罠と同型。

**2) 環境intern方式（採用候補）:** 名前文字列が識別子、番号は環境ローカル。
`HostPort::formatId(name)` をIFへ追加し、環境が登録時に名前をinternして
安定した非0 idを返す。デバイスは1回解決してキャッシュ。

| 検証 | 結果 |
| --- | --- |
| 衝突回避 | vendorとnodeが別idを取得（登録順で1と2）。同じpayloadでも誤解釈なし |
| 冪等性 | 同名の再登録は同じidを返す（host: cmd=1, again=1） |
| 解決コスト | 解決後100 frameでstrcmp 0回（整数比較のみ） |
| registry満杯 | 9個目で id 0 + `diag.fmt_full` イベント（hostで3回再現） |
| registry無し環境の縮退 | 既定 `formatId`=0 → 「id 0は何にも一致しない」規約でデバイスは安全に不活性（frameOut 0回、power 0のまま） |
| traceの可読性 | 環境が逆引きし `fmt=node.cmd` と名前で出力。未登録idは数値のまま（既存実験と互換） |

**3) 文字列のみ方式:** registryも登録手順も不要だが、100 frameでstrcmp 100回、
recordへ名前の複製が必要。**intern方式が解決1回・record 2byte・名前表示を
すべて満たすため不採用**。

**4) frameのbus id（同時に追加）:** 同一プロトコルの複数リンク（PIOの複数SM、
複数IRチャネル）を区別するため、`frameIn`/`frameOut` へデバイス論理bus番号を
追加した。正しいformatでもbus違いのframe（power offコマンド）は無視される
ことをhostで確認（telemetryは0401のまま）。

host側イベント列（7行、3回byte一致。format名がそのまま読める）:

```text
01 000000 main app frame.tx bus=0 fmt=node.cmd bits=16 data=0508
02 000000 main app frame.tx bus=0 fmt=node.cmd bits=16 data=0408
03 000000 main app frame.tx bus=0 fmt=vendor.cal bits=16 data=0408
04 000000 main app frame.tx bus=1 fmt=node.cmd bits=16 data=0400
05 001000 tick dev dev.frame bus=0 fmt=node.tel bits=16 data=0401
06 002000 tick diag diag.fmt_full name=overflow.x
07 002000 main dir dump node power=1 pending=0
```

**IFの変更:** `HostPort::formatId(name)` 追加、`frameIn`/`frameOut` にbus引数
（ヘッダ72→80行）。draft core側はEventのtext 44→56byte（1イベント72→80byte、
X22の値を更新）、`registerFormat` のregistry 8枠（名前20byte）。
X25のframe_port実験もbus付きへ改版した（手戻り記録: 理由は本節、host core 1.7.1）。

**専用portとframeの分担、ユースケースの線引き:** 所有者の問い
「SPI/I2Cだけ特別扱いか」「どこまでカバーし、何をやらないか」への答えは
[DEVICE_IF_SCOPE.ja.md](DEVICE_IF_SCOPE.ja.md)へまとめた。要点:
専用portは「Arduino標準APIが型付けし、masterが戻り値を待つ」3バス＋GPIO線＋
channelで**打ち止め**、以後の新プロトコルはすべてframe。絶対に手を出さない
領域（物理層・波形デコード・サイクル精度・アナログ回路網・エラッタ・
実時間race）を明文化した。

## X27. 最大データ量 — 決め打ちではなく環境とネゴする

対象: `tests/capacity/`

所有者の問い「IFの最大データ量は環境とネゴするのか決め打ちか」への検証。
**候補: ネゴ方式。** サイズ上限は環境の性質（記録バッファ、転送MTU）であって
プロトコルの性質ではないため、ポータブルなIFヘッダへ定数を焼き込まない。

**IFへの追加: `HostPort::maxFrameBits(bus)`（ヘッダ80→84行）。** 契約は
「デバイスは1回問い合わせて出力を分割する。上限内の呼び出しは全量受理を保証。
上限超過は契約違反として**黙って切り詰めず**、環境が診断イベントで可視化して
全量拒否する」（X19の教訓: 黙った欠落を作らない）。既定値0は「frame経路なし」
（`formatId`の0と同じ規約）で、デバイスは安全に不活性になる。

| 検証 | 結果 |
| --- | --- |
| 同一模型の適応（ネイティブ） | 32byteのサンプルを、64bit環境では**8byte×4 frame**、4,096bit環境では**32byte×1 frame**に自動分割。checksum一致（F0） |
| ネゴなし環境の縮退 | 既定`maxFrameBits`=0 → 出力0 frame（誤送信でなく不活性） |
| 上限内の保証（host、上限64bit） | 4 frame全量受理、要約表示 `len=8 sum=9C/DC/1C/5C` |
| 上限超過（host、128bitを送出） | `diag.frame_oversize bus=0 bits=128 max=64` で全量拒否。デバイス到達0回 |

インバウンド（環境→デバイス）はネゴ不要: IF契約上データはcall中のみ有効な
借用ポインタで、デバイスは必要分だけ読む（バッファ強制なし）。

## X28. 大量転送の記録粒度 — transaction集約サマリ

対象: `tests/bulk_spi/`

X10/Gate Fの宿題「大量転送を1byteずつ書くか塊で書くか」の実測。
draft coreへ「SPI transactionの内側では1byteごとの2行をやめ、endTransactionで
件数＋checksumの**サマリ1行**に集約する」候補を実装した（SCOPE 3.2の
「要約を残す」の具体化）。transaction外は従来のper-byte 2行のまま（X24互換）。

| 方式 | 256byte転送の記録 |
| --- | --- |
| transaction集約 | **2行**（`spi.begin` + `spi.bulk n=256 mosi_sum=80 miso_sum=80`）、欠落0 |
| per-byte 2行（transaction外100byteで実証） | 200記録の試行 → 64slotバッファで**146 dropped**（爆発の実証） |

```text
01 000000 main app spi.begin
02 000000 main app spi.bulk n=256 mosi_sum=80 miso_sum=80
03 000000 main app spi.req mosi=A0        ← transaction外は従来どおり
04 000000 main dev spi.resp miso=5F re=3
```

**事実:** 集約でもデバイスは1byteごとに呼ばれ続ける（応答の忠実性は不変）。
失うのはbyte単位のログ行だけで、件数と両方向checksumが完全性の代替になる。
transaction境界という自然な区切りが集約の開始・終了を決める。

**未決:** 集約をtransaction以外（`writeBytes`等の塊API、frame経路の連送）へ
広げる基準。checksumの種類（単純和で衝突が問題になるならCRC8等）。

## X29. 環境実装例#2 — 純粋C++の最小記録環境で同一模型を駆動

対象: `tests/native_env/`（環境実装例、`.ino`なし）、模型は `tests/common_models/`
（手戻り記録: 2026-09-04の契約改訂で`I2cTransfer`・schema照合・padding検査・
channel診断を追加し、環境#2は290行→328行。host側adapterは32行→36行）

「IFより上はプラットホーム別実装例」という整理を締めるため、host環境とは
独立に**2つ目の環境**を純粋C++11で書いた（`nenv::Env`、**290行**）。
`HostPort`の全実装、アプリ側bus API（`i2cWrite`/`i2cRead`/`serialWrite`/
`serialRead`/`delayMicros`）、進行役の注入（`chanWrite`/`dump`）、tick付き
仮想時計、host draftと同じ行形式のイベント記録を持つ。Arduinoもhost coreも
一切includeしない（テストが機械検査）。

模型はX23の温度センサとATモデムを**そのまま**使う。共有のため両模型を
`tests/common_models/`（Arduinoライブラリ形式）へ移し、host側は`sketch.yaml`の
`libraries: dir`、ネイティブ側は`-I`で同一ファイルを参照する（手戻り記録:
X23の配置変更、2026-09-04）。

X23と同じシナリオ（config書込み → 注入でDRDY → 温度読取り → AT+Sの遅延応答 →
dump）を環境#2で実行した結果（14行、3回byte一致）:

```text
01 000000 main app i2c.req addr=48 data=0105
02 000000 main dev i2c.resp status=0 re=1
03 000000 main dir chan.write chan=0 data=012C
04 000000 main dev gpio.inject line=0 val=1
05 000000 main app i2c.req addr=48 data=00
06 000000 main dev i2c.resp status=0 re=5
07 000000 main app i2c.rd.req addr=48 req=2
08 000000 main dev i2c.rd.resp len=2 data=012C re=7
09 000000 main app uart.tx AT+S
10 001000 tick dev dev.tx OK
11 001000 main app uart.rx O
12 001000 main app uart.rx K
13 001000 main dir dump temp=012C cfg=05
14 001000 main dir dump modem replies=1 pending=0
```

X23（host環境、18行）との対応:

| 内容 | host（X23） | native（X29） | 差の理由 |
| --- | --- | --- | --- |
| I2C config書込み req/resp | 01-02 | 01-02 | **同一** |
| `attachInterrupt` | 03 | なし | Arduinoアプリ側の概念 |
| 温度注入 chan.write | 04（ctx tick） | 03（ctx main） | 注入経路の差（hostは0us wait handler内、nativeは進行役が直接） |
| デバイスのDRDY線 | 05 `pin=27 0->1 match=1` | 04 `line=0 val=1` | pin対応とedge判定は環境の責務。nativeは論理lineのまま記録 |
| ISR enter/write/exit | 06-08 | なし | ISRはArduino側の機構 |
| 温度読取り req/resp ×2 | 09-12 | 05-08 | **同一**（seq以外） |
| uart.tx AT+S | 13 | 09 | **同一** |
| dev.tx OK（tick、1,000us） | 14 | 10 | **同一** |
| uart.rx O / K | 15-16 | 11-12 | **同一** |
| dump ×2 | 17-18 | 13-14 | **同一** |

アプリが観測した値も同一: t1=300、応答"OK"、経過1,000us。

**事実:**

- IFを挟んだ**デバイス側のイベント列は2環境で内容が一致**し、差は環境・
  アプリ側の機構（割り込み、pin対応、注入の呼び出し文脈）だけに閉じた。
  「環境側は実装例、IFが不変の境界」が2実装で成立
- 環境実装例#2は290行で、host側実装例（draft core + adapter 32行）より
  小さい。IFが要求する環境側の責務は小さく、決定的な再実装が容易
- 同一模型ソースを2環境が共有できることを、ファイル配置（`common_models/`）
  として固定した

## 固定前レビューへの対応（2026-09-04、X30〜X34）

所有者レビューで「embedbench_device.hを固定IFにするには契約不足」と指摘された
9点を、ヘッダ本文への契約明記と5つの実験で解消した。IFヘッダは84行→**106行**
（`I2cStatus`/`I2cTransfer`、frame packing helper、`frameOut`/`channelWrite`の
bool戻り値、`formatId`のschema引数、契約コメント）。影響を受けた既存実験は
新契約へ追随更新した（手戻り記録: X22/X23/X25〜X29。I2C行に`stop=`が付き、
format名が`<vendor>.<protocol>.<version>`形式に、`frameOut`/`channelWrite`が
bool、環境実装例#2は290→更新後の行数を各節に記載）。

| 指摘 | 対応 | 検証 |
| --- | --- | --- |
| 専用port条件とUARTの矛盾 | 分類を「同期要求応答bus / byte stream / 外部作用・環境入力 / frame」の4種へ再定義（SCOPE 1節） | 文書 |
| I2Cのtransaction情報不足 | `I2cTransfer{stop, continued}`とArduino準拠`I2cStatus` | X30 |
| frameのbit表現未定義 | MSB-first、末尾padding=0を環境が検査、bits=0は空frame（nullptr可） | X31 |
| 任意frameの自動分割は不可 | `maxFrameBits`=原子的に送れる最大。模型は分割せず、formatが分割を定義する場合のみ複数frame。超過はfalse+診断 | X27改・X31 |
| format名も衝突し得る | 命名規約`<vendor>.<protocol>.<version>`＋登録時schema照合（同名異schemaは衝突診断） | X34 |
| 再入規則の欠落 | 環境はDeviceを再入しない。Device内の`HostPort`呼び出しが引き起こす効果はDevice呼び出し完了後に配送 | X32 |
| 時間契約の不足 | 単調非減少・同値再呼び出し・飛び・reset・callback中の`nowMicros`をヘッダに明記 | X33 |
| channel/dumpの戻り値契約 | `channelWrite`は全量適用時のみtrue（falseは診断）、`channelRead`/`dump`はsnprintf規約 | X33 |
| 複数リンクの表現 | 「1 Deviceにつき各専用bus最大1つ、複合はadapterが子Deviceへ分割」を契約として明記 | 文書 |

## X30. I2Cのtransaction文脈（STOP・repeated start）

対象: `tests/i2c_transaction/`

repeated startを**要求する**register-map模型（pointer書込み→STOPなし→read で
データ、STOP後の単独readは応答しない）で、`I2cTransfer`の有効性を検証した。

| 操作 | native | host（Wire） |
| --- | --- | --- |
| pointer write（stop=0）→ read（continued=1） | 1 byte、0x5A | `endTransmission(false)`→`requestFrom`で1 byte、0x5A |
| pointer write（stop=1）→ read（continued=0） | 0 byte | `endTransmission(true)`→`requestFrom`で0 byte |
| register write後のrepeated start read | 0x77 | — |

```text
01 000000 main app i2c.req addr=50 data=01 stop=0
02 000000 main dev i2c.resp status=0 re=1
03 000000 main app i2c.rd.req addr=50 req=1 stop=1 rs   ← rs = repeated start
04 000000 main dev i2c.rd.resp len=1 data=5A re=3
05 000000 main app i2c.req addr=50 data=01 stop=1
06 000000 main dev i2c.resp status=0 re=5
07 000000 main app i2c.rd.req addr=50 req=1 stop=1
08 000000 main dev i2c.rd.resp len=0 data= re=7
```

**事実:** host coreのWire hookは`sendStop`を渡しており、環境が「直前の転送が
STOPなしで終わったか」を追跡すれば`continued`を機械的に導ける。X23の温度センサは
文脈を無視して動くので既存traceは`stop=`の付加以外変わらない。

## X31. frameのbit packingと原子性

対象: `tests/frame_bits/`

12bit commandをMSB-first（frame bit 0 = data[0]のbit 7）で受けるIR受信模型。

| 入力 | 結果 |
| --- | --- |
| {AB,C0} 12bit | 値0xABC（正しく復号） |
| {12,30} 12bit | 値0x123 |
| {AB,CF} 12bit（末尾4bitが非0） | 環境が`diag.frame_padding`で拒否、デバイス未到達 |
| bits=0, data=nullptr | 空frameとして受理、triggerとして数える |
| 模型の128bit status frame（環境上限64bit） | `frameOut`がfalse、模型はunsent=1を数え**分割しない** |

native（FakePortが同じ規則を実装）とhostで同一の結果、hostは7イベント・2診断。

## X32. 再入規則 — 応答中にIRQを上げるデバイス

対象: `tests/reentry/`

`i2cRead`の最中にIRQ線を上げる模型と、そのIRQで同じデバイスを再度読むISR。

| 配送方式 | デバイスの最大depth | 備考 |
| --- | ---: | --- |
| 即時（ISRをlineOut内で実行） | **2** | デバイスが自分のreadの中で再入される。契約違反の環境では模型に再入guardが必要になる |
| 延期（Device呼び出し完了後に配送、採用） | **1** | draft core: `deviceDepth`>0中のISRをキューし、応答記録後に配送 |

host trace（13行、2回一致）: `i2c.rd.req(04)` → `gpio.inject(05, dev)` →
`i2c.rd.resp(06)` → `isr.enter(07)` → ISR内のI2C 4行（isr文脈） → `isr.exit(12)`。
デバイスは常にdepth 1、`deferred=1`。

**事実（重要）:** デバイスの再入は防げるが、**アプリの観測値はFFFF**になった。
ISRは「バス転送完了時＝`Wire.requestFrom()`が戻る前」に走り、ISR内の別transaction
がWireの共有受信バッファを破壊したため。これはISR内でblockingバスI/Oを行う
アプリの実機と同種のバグであり、環境はそれを決定的に**そのまま観測**させる
（隠さない）。IF契約は「デバイスは再入されない」までを保証し、アプリ側の
バッファ破壊は対象外（SCOPE 3.2）。

## X33. channel / dump / 時間の契約

対象: `tests/contracts/`（ネイティブのみ、`common_models`の模型を使用）

| 契約 | 結果 |
| --- | --- |
| `channelWrite` 未対応channel / 長さ不正 / 正常 | false / false / true |
| `channelRead` cap=1 / cap=2 / 未対応 | 必要長2を返しcap分（1 byte）だけ書く / 2 / 0 |
| `dump` cap=8 | 必要長16を返し、出力は`temp=01`（7文字）でNUL終端 |
| `advanceTo(1000)`を2回 | 応答1回（二重発火なし） |
| 期限2000で`advanceTo(5000)`へ飛ぶ | 応答1回、時刻5000 |
| 保留中に`reset()`→`advanceTo(99999)` | 応答なし（保留期限は破棄） |

## X34. format名のschema照合

対象: `tests/format_schema/`

| 登録 | 戻り値 |
| --- | ---: |
| `acme.cmd.1` schema A1 | 1 |
| `acme.cmd.1` schema A1（再登録） | 1（冪等） |
| `acme.cmd.1` schema B2（同名・異仕様） | **0** + `diag.fmt_conflict` |
| `acme.cmd.2` schema B2（新版） | 2 |
| format 0 でのframe送出 | 拒否 + `diag.frame_noformat` |

**事実:** 名前だけでは独立ライブラリが同名を選べば衝突するため、schema指紋の
照合で「同名・異仕様」を登録時に検出できる。命名規約は`<vendor>.<protocol>.<version>`。

## 凍結前レビュー第2回への対応（2026-09-04、X35〜X37、X34追記）

所有者の第2回レビューで「凍結を止める」とされた5点と契約の小穴を、ヘッダ・
両環境実装例・新規3実験＋X34の拡張で解消した。IFヘッダは**実効LOC 120
（空行・コメント除く。物理314行）**。台帳では以後「実効LOC」と表記する。

| 指摘 | 対応 | 検証 |
| --- | --- | --- |
| 再入禁止がI2C経路だけ | draft coreで全Device経路を括る: pin forward（lineIn）、`bindTickDevice`（advanceTo。進行役の`setTickHandler`と分離）、frame受信側callbackは**Device実行中なら延期**。ISRとframe配送を1本の延期queueに統一 | X35 |
| 延期queue満杯の契約 | 容量超過は`diag.deferred_full`を記録して**破棄**（再入配送も無音欠落もしない）。ヘッダに「環境の延期容量まで保証、超過は診断＋破棄」を明記。draftは容量4 | X35（5発中1発破棄） |
| I2C `continued`がdevice単位で複数アドレスで壊れる | **bus単位**に変更: 直前転送のアドレスがSTOPなしで終わった場合だけ、同一アドレスへの次転送が`continued`。他アドレスへの転送はbusを閉じる（両環境実装例） | X36 |
| format名の長さ契約がなく19文字で切り詰め | `kFormatNameMaxLength = 19`をIF定数に。超過は0＋`diag.fmt_name_long`で**全量拒否、切り詰め禁止** | X34追記 |
| byte streamのchunk境界規則 | `serialIn`は呼び出し境界に意味を持たないbyte streamと明記。模型側がbuffering・parse（ATモデムは`;`終端で再実装） | X37 |
| 未登録の非0 format idでschema検査を迂回 | `frameOut`/`frameTx`はregistryに存在するidのみ受理（`diag.frame_unknown_format`） | X34追記 |
| `framePaddingClean`のnullptr参照、`frameBytes`のoverflow | nullptr→false、`bits/8 + (bits%8?1:0)`でoverflow不可 | ヘッダ |
| `channelRead`の0の曖昧さ | `kChannelUnsupported`（SIZE_MAX）を導入。0は正常な空 | X33更新 |
| `out != nullptr`（cap>0）の明記、`I2cStatus`範囲 | ヘッダに明記。範囲外statusは環境が`diag.i2c_status` | ヘッダ／両環境 |
| 未決: frameの`re=` | **決定**: 対応付けはformat payloadの責務。IFにframe単位のlinkは持たず、今後も増やさない | ヘッダ |
| 未決: schema指紋 | **決定**: `uint32_t`で凍結。生成規則として`schemaFingerprint(layout)`（FNV-1a）をヘッダに提供、手書き定数も可 | X34追記 |

手戻り記録: ATモデムを`;`終端のbyte-streamパーサへ再実装したため、X23/X29/X33の
入力が`AT+S;`に変わった（traceの`uart.tx AT+S;`）。X25のframe_portは未登録idが
拒否されるため名前解決へ変更（X26と同形）。環境実装例#2は`tests/common_env/`へ
移し（実効LOC 339）、repeated start要求のregister-map模型は`common_models`へ
移して複数実験で共有。adapterは`bindTickDevice`使用へ更新。

## X34追記. 名前長・未登録id・schema指紋

| 登録 / 送出 | 結果 |
| --- | --- |
| `acme.cmd.longnam.12`（19文字） | id 3、再登録も3 |
| `acme.cmd.longname.12`（20文字） | **0** + `diag.fmt_name_long len=20`（切り詰めない） |
| 未登録のid 7でframe送出 | 拒否 + `diag.frame_unknown_format bus=0 fmt=7` |
| schema | `schemaFingerprint("u8 addr,u8 cmd")` と `("u8 cmd,u8 addr")` が異なる指紋になり、同名登録は衝突として検出 |

## X35. 再入保証の全経路適用と延期容量

対象: `tests/reentry_paths/`

lineIn・frameIn・advanceTo・i2cReadの**全経路**から効果（IRQパルス、frame）を
出す模型で、draft coreの括りを検証した。

| 経路 | 発生源 | 配送 | 模型depth |
| --- | --- | --- | ---: |
| lineIn（pin forward） | `digitalWrite(4)`→lineIn内でIRQパルス | ISRはlineIn完了後 | 1 |
| advanceTo（`bindTickDevice`） | tick内でstatus frame送出 | アプリ受信shimはadvanceTo完了後に実行、shimのframeTxでframeIn | 1 |
| frameIn | 受信したcommandでIRQパルス | ISRはframeIn完了後 | 1 |
| i2cRead（burst） | 1回のread内で5パルス | 容量4: 4件延期配送、**5件目は`diag.deferred_full`で破棄** | 1 |

stats: `deferred_isrs=6 deferred_frames=1 dropped=1 device_depth=1 capacity=4`、
46イベント、2回一致。native（callbackのない環境）でもdepth 1。

**事実:** 括りを「Deviceを呼ぶ全ての場所」に置き、Device実行中に生じた効果を
1本のqueueへ集約すれば、経路によらずdepth 1が成立する。容量は環境の性質で、
超過は破棄でも診断イベントとして可視化される。

## X36. I2C repeated startはbusの状態

対象: `tests/i2c_multi/`（`common_models`のrepeated start要求模型×2、0x50/0x51）

```text
01 i2c.req addr=50 data=01 stop=0     ← Aがbusを開けたまま
03 i2c.req addr=51 data=01 stop=1     ← BのSTOPがbusを閉じる
05 i2c.rd.req addr=50 req=1 stop=1    ← rsなし → Aは応答しない（len=0）
07 i2c.req addr=50 data=01 stop=0
09 i2c.rd.req addr=50 req=1 stop=1 rs ← 真のrepeated start → len=1 data=5A
```

**事実:** device単位の`open`では05が誤って`rs`になり応答してしまう。bus単位の
「直前転送のアドレス＋STOP有無」で管理すると正しく区別できる。両環境実装例を
bus単位へ統一した。

## X37. serialはbyte stream — 呼び出し境界に意味を持たせない

対象: `tests/serial_stream/`（`common_models`のATモデム、`;`終端）

| 入力の与え方 | 応答 |
| --- | ---: |
| `"AT+S;"` を1回 | 1（+1,000us） |
| 1byteずつ5回 | 1（同じ） |
| `"AT"` + `"+S;"` の2回 | 1（同じ） |
| `"AT;AT;"` を1回 | 2（1回の呼び出しに2コマンド） |

host: `print("AT+S;")`は`uart.tx AT+S;`1行、1byteずつの`write`は`uart.tx A`〜`;`の
5行として**アプリの行為はそのまま記録**され、devの応答（`dev.tx OK`、+1,000us）は
どちらも同一。

## X38. serialOutのbinary契約（NUL入りbyte列）

対象: `tests/serial_binary/`

第3回レビューのブロッカー1。IFは「NULを含む任意byte列を運ぶ」と保証しているが、
host adapterが一度C文字列へ変換していたため `{0x41, 0x00, 0x42}` が0x41だけに
なっていた。IFとhost実装を次のように直した。

- `HostPort::serialOut` を **`bool` 戻り値**へ変更（全量queue成功でtrue）。
  受理しきれない場合は環境が診断イベントを出し残りを破棄、**部分配送を黙って
  行わない**
- `ebd::uartInject(origin, const uint8_t*, size_t)` へ変更し、`pushRx` の受理
  byte数を検査（不足なら `diag.uart_rx_full`）
- ログはprintableなら文字列、そうでなければhex（`data=410042`）。受信1byteは
  printableなら文字、そうでなければ `0x00` 表記

ATモデム模型に「`AT+B;` は `{0x41,0x00,0x42}` を即答」を追加して検証した。

| 検証 | 結果 |
| --- | --- |
| native（環境実装例#2経由） | アプリが3 byte受信 `410042`、ログは `dev.tx data=410042`、受信は `A` / `0x00` / `B` |
| host（draft core経由） | 同一（3 byte、同じ6行、2回byte一致） |

**事実:** IFのbinary契約が、両環境の転送経路とログの両方で成立する。
なお本実験は正常系（容量十分）だけを通す。容量不足時の分岐——受理prefixの配送、
残りの破棄、診断、falseの返却——はX41で別途固定した。**部分配送は起き得るが、
必ず診断を伴い、無音の欠落にはならない。**

## X39. i2cReadの不正な戻り長

対象: `tests/i2c_badlen/`

ブロッカー3。模型が `len + 1` を返すと、環境がその値をログ用bufferの長さとして
使い**buffer外を読む**。両環境に検査を入れ、意図的に契約違反する模型で確かめた。

| 操作 | 結果 |
| --- | --- |
| 2 byte要求に対し模型が3を返す | 環境が `diag.i2c_read_length addr=60 got=3 max=2` を記録し、`count = 0` として扱う |
| アプリが受け取る長さ | 0（不正な模型の主張を信用しない） |
| ログの応答行 | `i2c.rd.resp len=0 data=`（buffer外を描写しない） |

native（環境実装例#2）とhost（draft core）で同一。IFヘッダにも
「`len`超過は契約違反、環境は診断して何も供給されなかったものとして扱う」と明記した。

## X40. effect-freeメソッドの確定（再入管理の穴）

ブロッカー2。文書は「dumpを含む全Device経路で再入禁止」としていたが、両環境とも
`dump()` をDevice呼び出しとして括らずに直接呼んでいた。`channelRead` も同様で、
`reset()` は列挙から漏れていた。

**決定: `reset()` / `channelRead()` / `dump()` は effect-free（`HostPort`を呼んで
はならない）**とし、再入保証の対象外にする。効果を起こせない以上、環境はいつでも
直接呼んでよい。残る効果あり経路（`i2cWrite`/`i2cRead`/`spiTransfer`/`serialIn`/
`lineIn`/`frameIn`/`channelWrite`/`advanceTo`）が再入保証の対象で、これはX32/X35で
全経路を実測済み。

検証は `tests/contracts/` に追加した: 参照模型に対し `reset` / `channelRead`
（対応・未対応の両方）/ `dump` を呼び、`HostPort`への効果呼び出しが**0件**である
ことを確認する（`effect_free effects=0`）。

## X41. serialOutの容量不足経路

対象: `tests/serial_overflow/`

X38が正常系だけを通していたため、契約の残り半分——「受理できたprefixは配送、
残りは破棄、診断を記録、falseを返す」——を両環境で固定した。受信queueを8 byteへ
絞り、12 byteを返す模型で通す。ネイティブ環境例には受信容量を絞るAPI
（`setRxCapacity`、host coreの`setRxBufferSize`に相当）を足した。

| 検証 | native（環境実装例#2） | host（draft core） |
| --- | --- | --- |
| アプリが受け取るprefix | 8 byte `01234567` | 同一 |
| 残り4 byte | 届かない（追加読取り0 byte） | 届かない（`available()`=0） |
| 診断 | `diag.uart_rx_full accepted=8 len=12` が**1件だけ** | 同一（`diag=1`） |
| 模型が見る戻り値 | false（`refused=1`） | 同一 |
| 記録されない部分配送 | なし（`uart.rx` は受理8 byte分のみ） | 同一（12イベント、2回一致） |

```text
01 000000 main app uart.tx go
02 000000 main dev dev.tx 0123456789AB
03 000000 main diag diag.uart_rx_full accepted=8 len=12
04-11 000000 main app uart.rx 0 … 7
12 000000 main dir dump flood sent=1 refused=1
```

**事実:** 部分配送は起き得るが、必ず診断イベントを伴い、送信側の模型も
戻り値で知る。黙って消えるbyteはない。

## X42. デバイスIFの凍結と凍結ガード

対象: `src/embedbench_device.h`（version 1）、`tests/if_frozen/`

所有者の判断でデバイスIFを**凍結**した（2026-09-04）。決定内容と変更規則は
[DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md)。ヘッダ冒頭にも凍結宣言・変更規則・
`kDeviceInterfaceVersion`（=1）を書いた。

凍結ガード `tests/if_frozen/` が3点を固定する。

| 検査 | 内容 |
| --- | --- |
| 面の固定 | ヘッダの宣言行を抽出し、凍結一覧と**双方向**に照合（消えても、無断で増えても失敗）。実際に仮想関数を1つ足すと `undeclared additions to the interface` で落ちることを確認した |
| 単独ビルド | ヘッダだけをincludeする翻訳単位を `-Wall -Wextra -Werror -pedantic` でビルド。純粋virtualは`HostPort`の3つ、`Device`は`reset()`のみ |
| 既定の挙動 | 何もoverrideしないデバイスの応答（`i2cWrite`=2、`i2cRead`=0、`spiTransfer`=0xFF、`channelWrite`=false、`channelRead`=`kChannelUnsupported`、`dump`=0でNUL終端）と、frameを配送しない環境の応答（`frameOut`=false、`formatId`=0、`maxFrameBits`=0）、helper（`frameBytes`の0/1/8/9、padding検査のnullptr両ケース、指紋の一致・不一致） |

**副産物:** 検査コード自身に、printfの引数評価順に依存する不具合（X13で見たのと
同種）が入り込み、`dump_nul=0` として現れた。呼び出しを変数へ順序付けて解消した。

## X43. 環境の準拠キット（デバイス側からの契約検証）

対象: `tests/conformance/`、probeは `tests/common_models/src/conformance_probe.*`

凍結後の最初の実装課題。「環境側は実装例」という整理には、**環境が契約を
満たしているかを判定する共通の物差し**が要る。IFだけに依存する probe 模型を書き、
どの環境でも同じ標準シナリオを流して**判定（bitmask）で比較**する方式にした。

probeがデバイス側から観測できる契約:

| bit | 検査 | 判定方法 |
| ---: | --- | --- |
| 0x001 | 再入なし | 各inboundメソッドが自分を括り、開いたまま次が来ないこと |
| 0x002 | 時間の単調性 | `advanceTo` の値が前回以上 |
| 0x004 | 同一時刻の再呼び出し | 起きた場合に観測（**契約上は許可であって必須ではない**ので必須集合から除外） |
| 0x008 | 時計の整合 | メソッド内の `nowMicros()` が直近の `advanceTo` 以上 |
| 0x010 | 借用バッファ | call中に渡されたbyteが変化しない |
| 0x020 | 上限内frameの受理 | `frameOut` がtrue |
| 0x040 | 上限超過frameの拒否 | `frameOut` がfalse（切り詰めない） |
| 0x080 | format idの安定 | 同名・同schemaが同じ非0 idを返す |
| 0x100 | format名の長さ上限 | 20文字は0を返す |

| 環境 | 判定 |
| --- | --- |
| 環境実装例#2（純粋C++） | `ok=1 checks=1FB violations=0` |
| host（draft core） | `ok=1 checks=1FB violations=0`、環境自身の計測も `device_depth=1` |

**事実:** 同一のprobeと同一シナリオで、**2つの独立実装が同じ判定に達する**。
ログ形式や事象名の違いは判定に影響しない——環境の受け入れ試験として機能する。

**キットが空振りでないことの確認（negative test）:** 意図的に契約を破る環境
（デバイス呼び出しの中から再入、時間の逆行、上限超過frameの切り詰め受理）を
probeへ当てると `ok=0 violations=6` になる——内訳は時間逆行1、上限超過の受理1、
再入1、および直近の`advanceTo`より後ろの時計を見た3回。合格判定が
「何も検査していないから合格」ではないことを担保する。

**設計上の収穫:** 最初 `checks` の必須集合に「同一時刻の再呼び出し」を含めていたため
host環境が不合格になった。契約が**許可**しているだけの挙動を要求していた誤りで、
必須集合から外した。準拠キットは「契約が要求すること」だけを要求しなければならない。

## 次に必要な実験

（デバイスIFはX42で凍結済み。以下はIF外の実装例・ログ側の課題）

1. draft coreへAnalogを追加し、X22のシナリオを拡張して順序が保たれるかの確認
2. lifecycle連動のrun window（開始=preSetup、終了=指定loop回数の最後のpostLoop）をdraftへ実装
3. 1行形式のparse時間とdiff差分行数の比較（WP-B2の残り）
4. 検分を証拠に残す場合のEmbedBench経由dump経路の比較（X12の未決。dumpfは1案目）
5. SCOPE 5節の残り2項目（集約記録の基準とchecksum種別、bus別`maxFrameBits`下の
   format分割規則）はIF外なので、IFヘッダを「決定版」として凍結する判断へ
   （実験ではなく決定。凍結後の変更は台帳へ理由を残す）
