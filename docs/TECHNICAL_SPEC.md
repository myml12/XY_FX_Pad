# 技術仕様

このドキュメントは、JUCE製FXパッドアプリの実装詳細をまとめたものです。

## ソース構成

| ファイル | 役割 |
| --- | --- |
| `Source/Main.cpp` | `JUCEApplication` とメインウィンドウのエントリポイント |
| `Source/MainComponent.h/.cpp` | GUI、File/Inputモード、トランスポート、音声ルーティング、コントローラー更新 |
| `Source/XYPad.h/.cpp` | 正方形XYパッドの描画とマウス操作 |
| `Source/Effects.h/.cpp` | エフェクト列挙、表示ラベル、DSP本体、delay/roll用バッファ |
| `Source/Tempo.h/.cpp` | BPM推定とテンポセクションマップ |
| `Source/ControllerReceiver.h/.cpp` | 正規化されたコントローラーXYサンプルを受け取るUDP受信部 |
| `Tools/ControllerBridge.cpp` | 4点圧力式物理コントローラー用のシリアル-UDPブリッジ |

## 音声ルーティング

`MainComponent` は `juce::AudioAppComponent` を継承し、`setAudioChannels (2, 2)` でステレオ入力/出力を開きます。

- `File` モードでは `juce::AudioTransportSource` を音源として使い、出力前にエフェクトを適用します。
- `Input` モードでは入力バッファをそのまま出力へ流し、XYパッドを押している間だけエフェクトを適用します。
- X軸とY軸はそれぞれ独立した `EffectProcessor` を持つため、delay/reverb/phaser/roll などの状態は軸間で共有されません。
- エフェクトはモーメンタリー動作です。パッドを離すと delay/reverb/phaser/gate/roll 系の押下状態をリセットします。
- 終了時は、timer停止、controller停止、key listener解除、transport停止、source解除、reader解放、`shutdownAudio()` の順に処理します。

## エフェクト

すべてのエフェクト制御値は、`0.0..1.0` に正規化された軸値として渡されます。端点で破綻しにくくするため、多くのパラメータに `smoothstep` カーブまたはテンポ同期の段階値を使っています。

| エフェクト | 割り当て |
| --- | --- |
| `Gate` | 16分音符固定のBPM同期gate。軸値でdepth/wetを増加。 |
| `Echo` | BPM同期delay timeとfeedback/wet。 |
| `Reverb` | room sizeとwet mix。 |
| `Filter` | 左側でLPF、中央付近でnormal、右側でHPF。 |
| `Low Pass` | cutoffをスイープ。 |
| `High Pass` | cutoffをスイープ。 |
| `Band Pass` | 中心周波数をスイープ。Qを抑え、出力ゲインを補正。 |
| `Notch` | 中心周波数をスイープ。 |
| `Flanger` | rate、delay depth、wet、feedback。 |
| `Phaser` | rate、center frequency、feedback、mix。 |
| `Tremolo` | LFO depthとrate。 |
| `Drive` | soft clipping driveとwet。出力trimと簡易limiterあり。 |
| `BitCrusher` | 量子化量とwet mix。 |
| `Roll` | `1/32` から `1 bar` までのテンポ同期リピートとwet mix。 |

## BPM解析

`TempoAnalyzer` は音声ファイル読み込み時に解析を行い、全体BPMと小さなテンポセクションマップを生成します。

- 解析範囲: 先頭から最大600秒
- hop size: 1024 samples
- 特徴量: channel RMS energyのフレーム差分をonset-like envelopeとして使用
- BPM探索範囲: 70.0..190.0 BPM、0.5 BPM刻み
- 全体BPM: 解析範囲全体で自己相関スコアが最大の値
- ローカルBPM: 24秒window、4秒stepで自己相関し、全体BPM 28% + ローカルBPM 72%でブレンド
- 安定化: 全体BPM、半テン、倍テンに近い値へ吸着し、その後3点median filterを適用
- セクション化: 6 BPM未満の変化と1秒未満の短すぎる変化を無視
- 変更回数制限: 1曲あたり最大2回まで
- 再生時: 現在のtransport位置が属するテンポセクションを使用
- UI/音声処理用BPM状態: 同じ候補が1秒以上安定した場合だけBPMを切り替え

これはDJ用途向けの軽量実装です。安定したメインセクションを持ち、遅い前奏から速い本編へ移るような、少数回の大きなテンポ変化を想定しています。

## Nightcoreモード

NightcoreモードはFile再生専用です。`AudioTransportSource` に渡すsource sample-rate correctionを `1.18x` にして再接続することで、独立したタイムストレッチなしにピッチとテンポを同時に上げます。

おおよそのピッチ上昇量は以下です。

```text
12 * log2(1.18) ~= +2.9 semitones
```

ON/OFF切り替え時は、元音源上の再生位置を維持します。BPM表示とテンポ同期エフェクトには、再生倍率を反映したBPMを使います。
