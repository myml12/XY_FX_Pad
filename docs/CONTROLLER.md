# 物理コントローラー仕様

XY_FX_Pad の物理コントローラーは、4点の荷重から2次元座標を推定する圧力式FXパッドです。センサー読み取り、座標推定、GUI描画を分離し、音声処理側には軽量なXY/touch状態だけを渡します。

## ハードウェア

- MCU: ESP32-S3
- 荷重センサー: ロードセル x4
- ADC: HX711 x4
- HX711 RATE: 80Hz（RATEピンをVCC側）
- ロードセル想定: SC616C 500g
- シリアル: 115200 baud
- 物理フィールド比率: 3:2

4つのロードセルはフィールド四隅に対応します。

| CSVフィールド | 位置 |
| --- | --- |
| `g0` | 右上 |
| `g1` | 左上 |
| `g2` | 左下 |
| `g3` | 右下 |

## ファームウェア

[firmware.ino](../firmware.ino) はESP32-S3上で4つのHX711を読み、約80HzでCSVを出力します。

```text
g0,g1,g2,g3
```

起動時には各チャンネルのゼロ点をメジアンで推定します。

- 起動直後のウォームアップ: 80 samples（約1秒）
- ゼロ点推定: 120 samples（約1.5秒）
- 出力単位: gram相当値
- 出力形式: CSVのみ

## ControllerBridge

`ControllerBridge` はシリアル入力を読み、4点荷重をXY/touchへ変換してUDP送信します。

```sh
build/ControllerBridge
```

デフォルト設定:

- シリアルポート: `/dev/cu.usbmodem1101`
- ボーレート: `115200`
- UDP送信先: `127.0.0.1:45454`
- touch閾値: `5g`

コンソールには座標または未検出状態を表示します。

```text
XY 0.523,0.417 total=34.2g
検出なし total=2.1g
```

## 座標推定

負値は0に丸め、合計荷重から重心を求めます。

```text
total = max(g0,0) + max(g1,0) + max(g2,0) + max(g3,0)
x = (topRight + bottomRight) / total
y = (topLeft + topRight) / total
```

`total < 5g` の場合はtouchなしとして扱います。

実機の外周には有効操作できないマージンがあるため、Bridge側で10%の逆マージン補正を行います。物理的に届く `0.1..0.9` 付近の範囲を、ソフトウェア上の `0.0..1.0` に展開します。

## アプリ連携

JUCEアプリ本体はシリアルポートを開きません。`ControllerReceiver` がUDPで以下の形式を受信します。

```text
x,y,touch,total
```

音声処理側の `padX`, `padY`, `effectsActive` は受信サンプルごとに更新します。GUI描画は20Hz timerで最新サンプルだけを反映し、80Hzの制御応答と軽い描画負荷を両立します。
