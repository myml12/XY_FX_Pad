# XY_FX_Pad

XY_FX_Pad は、JUCEで実装したXYエフェクトパッド。マウス操作、外部音声入力、ESP32-S3ベースの4点圧力コントローラーを統合し、DJ向けのモーメンタリーFX操作を実験する。

X軸とY軸には独立したエフェクトを割り当てる。パッドを押している間だけDSPが有効になり、離すと各エフェクトのモーメンタリー状態をリセットする。

## 概要

- JUCE/C++17によるmacOSデスクトップアプリ
- X/Y軸それぞれに別エフェクトを割り当てる2軸FXパッド
- 音源ファイル再生と外部入力処理に対応
- 再生位置表示、シーク、Play/Pause、スペースキー操作
- BPM推定に基づく `Gate`, `Echo`, `Roll` のテンポ同期
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

DSP、BPM解析、音声ルーティングの詳細: [docs/TECHNICAL_SPEC.md](docs/TECHNICAL_SPEC.md)

## ハードウェア構成

物理コントローラーは、長方形フィールドの4隅に置いたロードセルから押下位置を推定します。

- MCU: ESP32-S3
- センサー: ロードセル x4
- ADC: HX711 x4
- HX711 RATE: 80Hz
- シリアル出力: `g0,g1,g2,g3` のCSV
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

## 音源ファイル

プロジェクトルートに配置した音源ファイルをアプリ内で選択可能。

```text
*.m4a
*.mp3
*.wav
*.aiff
```

## ディレクトリ構成

```text
.
├── CMakeLists.txt
├── Source/
│   ├── Main.cpp
│   ├── MainComponent.h/.cpp
│   ├── XYPad.h/.cpp
│   ├── Effects.h/.cpp
│   ├── Tempo.h/.cpp
│   ├── ControllerReceiver.h/.cpp
│   └── ControllerTypes.h
├── Tools/
│   └── ControllerBridge.cpp
├── docs/
│   ├── TECHNICAL_SPEC.md
│   └── CONTROLLER.md
└── firmware.ino
```

## ライセンス

MIT License
