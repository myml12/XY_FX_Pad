# XY_FX_Pad

XY_FX_Pad は、マウス操作と物理コントローラー操作の両方でDJ向けエフェクトを実験するための、JUCE製デスクトップFXパッドです。ビルドされるmacOSアプリ名は現在 `DJ XY Pad` です。

X軸とY軸にそれぞれ別のエフェクトを割り当てられます。パッドを押している間だけエフェクトが有効になるため、Kaoss Padのようなモーメンタリー操作を試せます。

## 主な機能

- マウスでドラッグできる正方形XYパッド
- X軸とY軸への個別エフェクト割り当て
- プロジェクトディレクトリ内の音源ファイル再生
- 外部入力音声を処理するInputモード
- 再生位置の表示とシーク
- `Play` / `Pause` ボタンとスペースキーによる再生停止
- ピッチとテンポを同時に上げるNightcoreモード
- `Gate`, `Echo`, `Roll` 向けの軽量BPM推定
- 4点圧力式物理コントローラー用のシリアル-UDPブリッジ

## エフェクト

利用できるエフェクトは以下です。

- `Gate`
- `Echo`
- `Reverb`
- `Filter`
- `Low Pass`
- `High Pass`
- `Band Pass`
- `Notch`
- `Flanger`
- `Phaser`
- `Tremolo`
- `Drive`
- `BitCrusher`
- `Roll`

DSPの割り当てやBPM解析の詳細は [docs/TECHNICAL_SPEC.md](docs/TECHNICAL_SPEC.md) を参照してください。

## 必要環境

- macOS（ローカルではApple Clang/CMakeで確認）
- CMake 3.22以上
- C++17対応コンパイラ
- JUCE

JUCEはこのリポジトリには含めません。以下のいずれかの方法で用意してください。

- `./JUCE` にJUCEをcloneする
- `find_package(JUCE CONFIG)` で見つかるようにJUCEをインストールする
- ネットワークが使える環境で `-DDJXYPAD_FETCH_JUCE=ON` を指定する

## ビルド

`./JUCE` にJUCE cloneがある場合:

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

## 音源ファイル

ビルド前に、ローカルのテスト用音源をプロジェクトルートに置いてください。

```text
*.m4a
*.mp3
*.wav
*.aiff
```

CMakeビルド時に、これらのファイルはapp bundleのResourcesへコピーされます。音源ファイルはgitignoreしているため、著作権のある音源やローカル専用素材を誤って公開しないようになっています。

## 物理コントローラー

Arduinoファームウェアは [firmware.ino](firmware.ino) です。約80Hzで4点の圧力値を出力します。

プロジェクトをビルドしたあと、別ターミナルでブリッジを起動します。

```sh
build/ControllerBridge
```

デフォルトのシリアルポートは `/dev/cu.usbmodem1101` です。ブリッジは4点の圧力値を正規化XY座標に変換し、UDPでアプリへ送信します。プロトコルと座標計算の詳細は [docs/CONTROLLER.md](docs/CONTROLLER.md) を参照してください。

## プロジェクト構成

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

このプロジェクトはMIT Licenseで公開しています。JUCEおよびその他のサードパーティ依存関係は、それぞれのライセンスに従います。
