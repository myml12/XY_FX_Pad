# 技術仕様

XY_FX_Pad は、JUCEのリアルタイム音声処理と、外部物理コントローラーからのXY入力を組み合わせたモーメンタリーFXシステムです。音声処理、GUI、シリアル入力を分離し、パッド操作に対する応答を優先した構成にしています。

## ソース構成

| ファイル | 役割 |
| --- | --- |
| `Source/Main.cpp` | `JUCEApplication` とメインウィンドウ |
| `Source/MainComponent.h/.cpp` | GUI、transport、音声I/O、XY状態管理 |
| `Source/XYPad.h/.cpp` | XYパッド描画、マウス操作、外部座標表示 |
| `Source/Effects.h/.cpp` | エフェクト定義とDSP実装 |
| `Source/Tempo.h/.cpp` | BPM推定、テンポセクション管理 |
| `Source/ControllerReceiver.h/.cpp` | UDP経由のXY/touch受信 |
| `Tools/ControllerBridge.cpp` | シリアル入力、重心計算、UDP送信 |

## 音声ルーティング

`MainComponent` は `juce::AudioAppComponent` としてステレオ入出力を開きます。

- `File` モード: `AudioTransportSource` で音源を再生し、出力前にFXを挿入
- `Input` モード: 入力バッファを直接処理し、外部音声にFXを適用
- X軸/Y軸: それぞれ独立した `EffectProcessor` を保持
- 押下中のみFX有効、離したタイミングでモーメンタリー状態をリセット
- `Roll` とdelay系は常時履歴を書き込み、押下中にのみ読み出し/混合

## XY制御

パッド座標は `0.0..1.0` の正規化値です。

- マウス操作: `XYPad` が直接 `padX`, `padY`, `effectsActive` を更新
- 物理操作: `ControllerReceiver` がUDPサンプルを受け、同じ状態変数へ反映
- GUI描画: 20Hz timerで外部座標表示を更新
- 音声処理: 最新のXY/touch状態をaudio callback内で参照

## エフェクト設計

各エフェクトは1軸の値を受け取り、音量・周波数・時間・wet量などへ変換します。端点で破綻しにくいよう、連続値には `smoothstep` 系のカーブを使い、テンポ同期系は段階値に量子化しています。

| エフェクト | 制御内容 |
| --- | --- |
| `Gate` | 16分音符固定。軸値でdepth/wetを制御。 |
| `Echo` | BPM同期delay time、feedback、wet。 |
| `Reverb` | room size、wet mix。 |
| `Filter` | LPF、normal、HPFを1軸上でモーフィング。 |
| `Low Pass` | cutoff sweep。 |
| `High Pass` | cutoff sweep。 |
| `Band Pass` | center frequency sweep。Q抑制とゲイン補正あり。 |
| `Notch` | center frequency sweep。 |
| `Flanger` | LFO rate、delay depth、feedback、wet。 |
| `Phaser` | rate、center frequency、feedback、mix。 |
| `Tremolo` | LFO rate、depth。 |
| `Drive` | soft clipping drive、wet、output trim。 |
| `BitCrusher` | quantization depth、wet。 |
| `Roll` | `1/32` から `1 bar` までのテンポ同期リピート。 |

## BPM解析

`TempoAnalyzer` は音源読み込み時に軽量なonset envelopeを作り、自己相関でBPMを推定します。目的は厳密な音楽解析ではなく、FXのテンポ同期に使える安定したBPM状態を得ることです。

- 解析範囲: 先頭から最大600秒
- hop size: 1024 samples
- 特徴量: channel RMS energyのフレーム差分
- 探索範囲: 70.0..190.0 BPM、0.5 BPM刻み
- 全体BPM: 全解析範囲の自己相関最大値
- ローカルBPM: 24秒window / 4秒step
- 安定化: 全体BPM、半テン、倍テンへの吸着 + 3点median filter
- セクション化: 6 BPM未満、1秒未満の変化を無視
- 変更回数: 1曲あたり最大2回
- 適用: 同じ候補が1秒以上続いた場合のみBPM状態を切り替え

この設計は、遅いイントロから一定テンポの本編へ移るような曲を想定しています。細かいBPM揺れを追うのではなく、FX操作に使いやすいテンポ状態へ丸めます。

## Nightcoreモード

NightcoreモードはFile再生時のみ有効です。`AudioTransportSource` のsource sample-rate correctionを `1.18x` にし、ピッチとテンポを同時に上げます。

```text
12 * log2(1.18) ~= +2.9 semitones
```

切り替え時は元音源上の再生位置を維持します。BPM表示とテンポ同期エフェクトには、再生倍率を反映したBPMを使います。
