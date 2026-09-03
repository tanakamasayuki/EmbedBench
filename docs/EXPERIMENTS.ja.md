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

## 次に必要な実験

1. 0 us waitで外部処理だけを回す場合の再入・無限ループ条件
2. 固定tick境界で進行役が別の待ちAPIを呼んだ場合の禁止・失敗方法
3. 1つの下位hookから複数listenerへ配る最小構造と、登録上限到達時の挙動
4. イベント列の候補ごとの1件あたりのサイズ、固定バッファ容量、欠落の通知方法
5. Wireの観測者と応答デバイスを分けても戻り値が一意に決まる構造

## X6. host拡張点の不足

対象: `tests/host_gaps/`

| 操作 | 実測 |
| --- | ---: |
| GPIOをLOW/HIGHへ変更 | edge 2、ISR callback 0回 |
| `analogRead`後のread hook回数 | 1回 |
| 続けて`analogReadMilliVolts`後のread hook回数 | 1回のまま |
| UART TX | 2byteをqueueへ保存、2byteを後からdrain可能、同期activity hookなし |

不足する能力と依頼候補は[HOST_EXTENSION_AUDIT.ja.md](HOST_EXTENSION_AUDIT.ja.md)へ分離した。
