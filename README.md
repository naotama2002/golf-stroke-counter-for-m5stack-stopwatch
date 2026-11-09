# PixelGolf — Golf stroke counter for M5Stack StopWatch

[PixelGolf for Apple Watch](https://github.com/naotama2002/golf-stroke-counter-for-apple-watch) を
[M5Stack StopWatch](https://docs.m5stack.com/ja/core/StopWatch)（ESP32-S3 / 1.75" 円形 AMOLED 466x466）に移植した試作ファーム。

ドット絵テイストの打数カウンター。コース情報（パー）は扱わず、打数だけを数える。

## 使い方

| 操作 | タッチ | 物理ボタン |
| --- | --- | --- |
| ラウンド開始 | **PLAY START** | BtnA |
| 1 打カウント | 大きな **+** | BtnA クリック |
| 打ち間違いを戻す | 小さな **−** | BtnB クリック |
| ホールアウト（18 番で自動終了） | **HOLE OUT** | BtnA 長押し |
| 合計 / OUT・IN / スコア表 | 左右フリック | — |
| 終わったホールの打数を修正 | スコア表のマスをタップ | — |
| ラウンド終了 | **END ROUND** | — |
| 今すぐ電源 OFF | — | BtnB 3 秒長押し |

状態は操作ごとに NVS（フラッシュ）へ保存される。電源が落ちても、電源ボタンで起動すれば続きから再開する。

## 省電力（450mAh で 1 ラウンド持たせるための工夫）

| 無操作時間 | 状態 |
| --- | --- |
| 0〜6 秒 | 通常表示 |
| 6 秒〜 | 減光（輝度 160 → 24） |
| 12 秒〜 | 画面 OFF、CPU 240MHz → 80MHz |
| — | 自動電源 OFF はしない。BtnB 3 秒長押しで電源 OFF、電源ボタンで起動すると続きから |

- 画面 OFF 中の最初のボタン / タッチは「起こすだけ」で、打数には数えない（ポケットの中の誤カウント対策）
- ホールアウト時は演出の前に保存するので、演出中に電池が切れても記録は残る
- 時間はすべて `src/main.cpp` の `cfg` で変更できる

> ⚠️ 消費電流は実機で未計測。1 ラウンド（4〜5 時間）持つかは実測で確認する必要がある。

## 開発

```shell
# 実機ファームのビルド
pio run -e m5stack-stopwatch

# 書き込み（リセットボタン 2 秒長押しで緑 LED → ダウンロードモード）
pio run -e m5stack-stopwatch -t upload

# ロジックのユニットテスト（PC 上で実行、実機不要）
pio test -e native
```

## USB シリアル（デバッグ用）

PC と USB ケーブルで直結しているときだけ使える（115200bps）。Wi-Fi / Bluetooth は使っていないので、無線からは操作できない。

- ログは USB に流すだけで、本体のフラッシュには書かない（ストレージは消費しない）
  - `[hb]` 5 秒ごと: 経過時間・画面状態・ボタン/タッチ・充電中か・電池残量 % と電圧 mV
  - `[btn]` `[touch]` `[input]` `[power]` 操作と画面 / 電源の状態変化
- 1 文字コマンド: `s` START / `+` / `-` / `o` HOLE OUT / `x` END ROUND / `n` TOP / `F` 1 番の修正画面 / `C` 終了確認 / `d` 状態表示 / `R` 再起動 / `P` 電源 OFF

```shell
pio device monitor   # ログを見る・コマンドを送る
```

## 構成

- `src/main.cpp` — 画面描画・タッチ / ボタン・振動・電源管理（M5Unified / M5GFX / M5PM1）
- `lib/golfcore/` — ハードウェアに依存しないロジック
  - `round.{h,cpp}` — ラウンド状態（`RoundStore.swift` の移植）と保存形式
  - `power_policy.h` — 減光 → 画面 OFF の状態遷移
  - `pixel_font.h` / `sprites.h` — 5x7 ドットフォントとボールくん（Swift 版から変換）
- `test/test_round/` — `golfcore` のユニットテスト（Unity）

## TODO（実機が届いたら）

- [ ] 消費電流・1 ラウンドの電池持ちを計測
- [ ] 円形画面でのレイアウト・タップ領域の微調整
- [ ] 画面 OFF 中の ESP32 light sleep（BtnA / タッチ割り込みで復帰）
- [ ] IMU で「手首を上げたら点灯」
