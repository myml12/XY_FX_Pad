# XY_FX_Pad

XY_FX_Pad は、JUCEで実装したXYエフェクトパッド。マウス操作、外部音声入力、ESP32-S3ベースの4点圧力コントローラーを統合し、DJ向けのモーメンタリーFX操作を実験する。

X軸とY軸には独立したエフェクトを割り当てる。パッドを押している間だけDSPが有効になり、離すと各エフェクトのモーメンタリー状態をリセットする。

## 概要

- JUCE/C++17によるmacOSデスクトップアプリ
- X/Y軸それぞれに別エフェクトを割り当てる2軸FXパッド
- 音源ファイル再生と、BlackHole経由のシステム音声キャプチャに対応
- 再生位置表示、シーク、Play/Pause、スペースキー操作
- BPM推定に基づく `Gate`, `Echo`, `Roll` のテンポ同期
- 出力音の周波数帯域を常時表示するLive Spectrum
- ピッチとテンポを同時に上げるNightcoreモード
- ESP32-S3 + ロードセル + HX711による4点圧力コントローラー入力
- シリアル処理を別プロセス化し、UDPでアプリへXY座標を送信

## エフェクト

| エフェクト | 用途 |
| --- | --- |
| `Gate` | 16分音符固定のテンポ同期ゲート |
| `Echo` | テンポ同期ディレイ |
| `Reverb` | 空間系リバーブ |
| `Filter` | LPF/Normal/HPFを横断するDJフィルター |
| `Low Pass` | ローパスフィルター |
| `High Pass` | ハイパスフィルター |
| `Band Pass` | バンドパスフィルター |
| `Notch` | ノッチフィルター |
| `Flanger` | 短い遅延変調によるフランジャー |
| `Phaser` | 位相変調系フェイザー |
| `Tremolo` | LFO音量変調 |
| `Drive` | ソフトクリップ系ドライブ |
| `BitCrusher` | 量子化ビットクラッシュ |
| `Roll` | テンポ同期ループロール |
| `Peak EQ` | 筆圧で強くなる周波数強調 |
| `Ladder` | 筆圧でレゾナンス／ドライブが増すローパス |
| `Chorus` / `Auto Pan` | 空間的な揺らぎ |
| `Compressor` | 音量の密度調整 |

## 書道パフォーマンス

`Performance preset` から、文字を書く動きに合わせた10種類の割当を選べます。名前は表示環境に依存しないASCII表記です。まずは `Ink Accent` を推奨します。X方向で強調する周波数を選び、筆圧でその強さが増すため、筆の太さと音のアクセントが自然に対応します。

- `Ink Bleed` / `Soft Wash`: 払いとにじみを空間系へ結び付ける。
- `Rhythm Strokes` / `Glitch Dots`: 点・止め・跳ねをBPM同期のリズムへ結び付ける。
- `Sweep Calligraphy` / `Echo Script`: 書く位置そのものを音色・残響の動きにする。
- `Bass Stroke` / `Bright Stroke` / `Air Brush`: 太さ、明るさ、空間のキャラクターを変える演出用。

SDVX風の演出には、`SDVX Laser Wobble`、`SDVX FX Gate`、`SDVX Retrigger`、`SDVX Jet Flanger`、`SDVX Noisy Filter`、`SDVX Live Reverb` も用意しています。左右の筆運びをゲームの2つのつまみ、筆圧をFXボタン長押しとして扱う、プレイ感を重視した再構成です。

実演時は、単発の強い誤タッチを避けるため、Bridgeのゼロ点校正をパッド無荷重で終えてから開始してください。UDPが200ms途切れた場合は自動でFXが解除されます。

DSP、BPM解析、音声ルーティングの詳細: [docs/TECHNICAL_SPEC.md](docs/TECHNICAL_SPEC.md)
エフェクトごとの実際の音声処理と聴こえ方: [docs/EFFECTS.md](docs/EFFECTS.md)

## ハードウェア構成

物理コントローラーは、長方形フィールドの4隅に置いたロードセルから押下位置を推定します。

- MCU: ESP32-S3
- センサー: ロードセル x4
- ADC: HX711 x4
- HX711 RATE: 80Hz
- シリアル出力: `raw0,raw1,raw2,raw3` のCSV（g換算とフィルターはPC側）
- 座標化: PC側の `ControllerBridge` で重心を計算
- アプリ連携: `127.0.0.1:45454` のUDPでXY/touchを送信

物理コントローラーとブリッジの詳細: [docs/CONTROLLER.md](docs/CONTROLLER.md)

## 必要環境

- macOS
- CMake 3.22以上
- C++17対応コンパイラ
- JUCE

JUCEは以下のいずれかで解決します。

- `./JUCE` にJUCEを配置する
- `find_package(JUCE CONFIG)` で検出できるようにする
- `-DDJXYPAD_FETCH_JUCE=ON` でCMakeから取得する

## ビルド

`./JUCE` にJUCEがある場合:

```sh
cmake -S . -B build
cmake --build build
open build/DJXYPad_artefacts/DJ\ XY\ Pad.app
```

CMakeでJUCEを取得する場合:

```sh
cmake -S . -B build -DDJXYPAD_FETCH_JUCE=ON
cmake --build build
open build/DJXYPad_artefacts/DJ\ XY\ Pad.app
```

物理コントローラーを使う場合は、アプリとは別にブリッジを起動します。

```sh
build/ControllerBridge
```

## システム音声キャプチャ（PC Audio モード）

macOS ではアプリ単体で内部音声を取れないため、仮想オーディオデバイス [BlackHole](https://github.com/ExistentialAudio/BlackHole) を使う。

**ジレンマと解決:** システム出力を BlackHole だけにするとスピーカーから聞こえなくなる。本アプリは `PC Audio` 切替時にシステム出力を自動で BlackHole へ向け、アプリがスピーカーへ通す（FX 付き）。`Track` に戻すとシステム出力も元に戻る。

1. BlackHole を clone（未取得の場合）:

```sh
git clone https://github.com/ExistentialAudio/BlackHole.git
```

2. ドライバをビルドしてインストール:

```sh
chmod +x Tools/install_blackhole.sh
./Tools/install_blackhole.sh
```

3. アプリを起動し、`Track` → `PC Audio` に切り替える

   - システム出力: 自動で BlackHole
   - アプリ入力: BlackHole
   - アプリ出力: スピーカー / ヘッドホン
   - パッド押下中だけ FX、離すとドライ通過

`Track` に戻すと入力を閉じ、システム出力を復元する。アプリを強制終了した場合は、システム設定の出力先を手動で戻す。

## 音源ファイル

`audio/` に配置した音源ファイルをアプリ内で選択可能。音源本体は `.gitignore` 対象で、リポジトリには含まれない。

```text
audio/
├── *.m4a
├── *.mp3
├── *.wav
└── *.aiff
```

## ディレクトリ構成

```text
.
├── CMakeLists.txt
├── audio/
├── Source/
│   ├── Main.cpp
│   ├── MainComponent.h/.cpp
│   ├── XYPad.h/.cpp
│   ├── Effects.h/.cpp
│   ├── Tempo.h/.cpp
│   ├── ControllerReceiver.h/.cpp
│   └── ControllerTypes.h
├── Tools/
│   ├── ControllerBridge.cpp
│   └── install_blackhole.sh
├── docs/
│   ├── TECHNICAL_SPEC.md
│   ├── CONTROLLER.md
│   └── EFFECTS.md
├── BlackHole/          # 任意: システム音声キャプチャ用（gitignore）
└── firmware.ino
```

## ライセンス

MIT License
