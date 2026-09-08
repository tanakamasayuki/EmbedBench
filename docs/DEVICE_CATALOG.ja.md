# デバイス模型カタログと format 名の運用

内部の記録。日本語のみ。凍結したデバイスIF（[DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md)）で
書いた模型を、どこに置き、どう名前を付け、どう検証するかを定める。
IFの外側の**運用**であり、IFの変更を伴わない。

## 1. 模型の置き場所

| 置き場所 | 用途 | 例 |
| --- | --- | --- |
| `devices/src/` | 複数の実験・複数の環境から共有する参照模型 | 温度センサ、ATモデム、register-map、準拠probe、環境センサー（BME280相当）、GPS受信機 |
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

実デバイス相当の模型。M5StackのUnit系に代表される形を、バスの種類ごとに
一通り揃えてある（SPIはUnitではなくBASE側の周辺機器なので `spi_device/` の
表示器模型が受け持つ）。

| 模型 | 相当するUnit | 使う経路 | その形でしか出ない特徴 |
| --- | --- | --- | --- |
| `unit_button_model` | BUTTON | `lineOut`、channel | バスを持たない最小の形。押下でlowへ引く |
| `unit_pir_model` | PIR | `lineOut`、`requestWake`、channel | 動きが止まった後も保持時間だけ線が上がったまま |
| `unit_relay_model` | RELAY | `lineIn`、`diagnose`、channel | アプリが線を駆動する向き。接点が落ち着く前の再切替を通知 |
| `unit_sonic_model` | Ultrasonic I/O | `lineIn`、`lineOut`、`requestWake`、`diagnose`、channel | **測距がパルス幅**。環境が要求どおりの時刻に端を置けるかが出る |
| `unit_angle_model` | ANGLE | `analogOut`、`analogOutMilliVolts`、channel | 生の数値とmVの両方を提示（換算は環境が持たない） |
| `unit_light_model` | LIGHT | `analogOut`、`lineOut`、channel | 1つの物理量から**アナログと閾値デジタルの2出力** |
| `unit_encoder_model` | ENCODER | I2C（plain read可）、`diagnose`、channel | 符号つきcounter、アプリからの書込み（LED・ゼロ復帰） |
| `unit_modbus_model` | RS485 | serial（**バイナリ**）、`requestWake`、`diagnose`、channel | 終端文字でなく**沈黙で区切るframing**、CRC16、不正フレームは無応答 |
| `env_sensor_model` | ENV系 | I2C（repeated start必須）、`lineOut`、`requestWake`、`diagnose`、channel | 7.5 msの測定時間、status register、未定義registerの通知 |
| `gps_model` | GPS | serial（行指向）、`requestWake`、`diagnose`、channel | 自走する周期送信、本物と同じchecksum、コマンド拒否 |
| `unit_ir_model` | IR | frame（bus 0）、`requestWake`、channel | アドレス無しのbroadcast、**payloadを持たない空フレーム**の繰り返し符号 |
| `unit_lora_model` | LoRa | frame（bus 1）、`lineOut`、`requestWake`、`diagnose`、channel | **空中時間がpayload長に比例**、送信中は落とす、RSSIはpayloadの外 |
| `unit_uwb_model` | UWB | frame（bus 2）、`requestWake`、`diagnose`、channel | **同一模型を複数実体**で1リンクに載せ順番に応答、**12ビット**のbit-packed poll |
| `unit_imu_model` | IMU | I2C、`lineOut`、`requestWake`、`diagnose`、channel | **自走サンプリング**、深さ16のFIFO、**読み出し長がデバイス状態次第**、遅刻すると取りこぼす |
| `unit_rtc_model` | RTC | I2C、`lineOut`、`requestWake`、`diagnose` | カタログで唯一**自分の時刻の単位（秒）を持つ**。アラームは時刻指定 |
| `unit_chunk_model` | （汎用中継） | frame、`requestWake`、`diagnose`、channel | **bus別容量に応じた分割規則**の実例。載らないリンクは通知して拒否 |
| `unit_flash_model` | （BASE周辺） | SPI、`lineIn`、`requestWake`、`diagnose`、channel | **順序で意味が変わる**。write-enable無しの書き込みを通知して捨てる |
| `unit_codec_model` | （BASE周辺） | **I2C + SPI**、`lineIn`、`diagnose` | **1つの模型が2本のバスに載る**。制御バスの設定がデータバスの応答を変える |
| `unit_faulty_model` | （故障注入） | I2C、channel | **わざと壊れる**唯一の模型。無応答・拒否・途中で切れる読み出し・間欠故障 |
| `unit_sdcard_model` | SD/TFカード | SPI、`lineIn`、`requestWake`、`diagnose`、channel | **ブロックデバイス**（512バイト×8）。FATは載せない——それはアプリの仕事 |
| `regtable_model` | （汎用・表駆動） | I2C、`diagnose`、channel | **表とフックの分離**。register-mapを`RegTableSpec`の表で持ち、表で言えない振る舞いは派生クラスの`onRead`/`afterWrite`へ。キャプチャから表を生成する入口（下記） |
| `tape_model` | （汎用・録画再生） | I2C、serial、SPI、`requestWake`、`diagnose` | **録画を1ステップずつ再生**。外れた要求を診断で名指しし、その後も応答を返し続ける。分岐は持たない（X64・X65） |

### ファイルシステムはデバイス側に無い

SDカードが提供するのは番号のついたブロックだけで、FATを組み立てるのは
検証対象のライブラリである。だから模型はブロックデバイスとして書き、
プリセット（`devices/src/sd_images.h`）に**本物のFAT12イメージ**を7枚持たせて、
アプリが本当に解析できるか、そして**壊れたボリュームで正しく失敗するか**を検査する。
壊れ方は、最初の検査で弾かれるもの（boot signature破損）だけでなく、
途中まで正常に見えるもの——巡回した鎖、範囲外のクラスタ、
サイズと鎖の不一致、ゼロのbytes-per-sector——を揃えてある。自前のイメージは
`loadBlocks()` でそのまま流し込める。プリセットの生成器は
`devices/tools/make_sd_images.py` で、構築したイメージを
ドライバと同じ手順で解析し直してからCソースを出す。

### キャプチャから雛形を作る

`devices/tools/trace2regtable.py` は、両環境が出す1行1イベントのトレース
（実機側のシムが同じ行を時刻だけ付けて出してもよい）から、`regtable_model` の
表を持つヘッダと、**推定できなかった点のTODO一覧**を作る。表に入るのは
レジスタ・幅・電源投入値・書込可否・repeated start要否・channelの素通し対応まで。
時間で変わったレジスタ、動いた線、要求より短い読み出し、ログが切り詰めた
payload、素通しでないchannelはTODOとして残り、生成クラスを継承した
別ファイルのフックに人が書く（`tests/capture_scaffold/native/hooks.h` が実例）。
**JSONで振る舞いまで書こうとしない**——表はデータのまま、規則言語にはしない（X63）。

キャプチャの入口は3つ（X64・X65）。実機側のシム `capture/`（`CaptureWire` は `TwoWire` 派生、
`CaptureSerial` は `Stream` 派生、`CaptureSPI` はラッパー。実体へ転送しつつ同じイベント行を
`micros()` 付きで出す。全payloadを運ぶ。バスだけでは**線が見えない**ので、部品が駆動する線は
`CaptureLines` でポーリングして足す）、ロジックアナライザのバスレベル出力を同じ行へ畳む
`devices/tools/la2trace.py`（無改造で線も取れるが変換が1段増える）、そして
EmbedBench自身のトレース（payloadは5バイトまでなので雛形には足りるがtapeには足りない）。
キャプチャ直後の一手目は `devices/tools/trace2tape.py` で作る `tape_model` の録画再生で、
「アプリが同じ道を通るか」を確かめてから表＋フックへ進む。

### `reset()` は「新品」ではなく「電源投入」

凍結IFの `reset()` は「電源投入状態へ戻し、保留中の時刻を全て捨てる」。
揮発性の模型では「まっさらな新品」と同じになるので差が出ないが、
**不揮発な部品では書いた内容は残る**（X58）。`unit_flash_model` が実例で、
`reset()` は配列に触れず、新品状態（全て0xFF）は構築時に作り、
消去はchip eraseコマンドでのみ起きる。不揮発の状態を持つ模型を書くときは
この区別を守ること。進行中の書き込みは `reset()` で落とす（契約どおり）。

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
