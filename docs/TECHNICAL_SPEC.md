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
| `Source/PressureMeter.h` | 筆圧の表示メーター |
| `Source/SystemAudioRouter.h/.cpp` | macOSのBlackHole／既定出力切替 |
| `Tools/ControllerBridge.cpp` | シリアル入力、重心計算、UDP送信 |

## 音声ルーティング

`MainComponent` は `juce::AudioAppComponent` としてステレオ入出力を開きます。

- `Track` モード: `AudioTransportSource` で音源を再生し、出力前にFXを挿入（入力デバイスは開かない）
- `PC Audio` モード: BlackHole を入力、スピーカー等を出力に自動切替し、システム音声にFXを適用
- X軸/Y軸: それぞれ独立した `EffectProcessor` を保持
- 押下中のみFX有効、離したタイミングでモーメンタリー状態をリセット
- `Roll` とdelay系は常時履歴を書き込み、押下中にのみ読み出し/混合
- `PC Audio` を離れると入力デバイスを閉じ、マイク権限の保持を解除する

システム音声キャプチャの前提:

1. BlackHole HAL プラグインがインストールされていること（`Tools/install_blackhole.sh`）
2. `PC Audio` 切替時にシステム既定出力を BlackHole へ自動変更し、アプリがスピーカーへモニタする
3. `Track` 復帰時（およびアプリ終了時）にシステム既定出力を復元する

## XY制御

パッド座標は `0.0..1.0` の正規化値です。

- マウス操作: `XYPad` が直接 `padX`, `padY`, `effectsActive` を更新
- 物理操作: `ControllerReceiver` がUDPサンプルを受け、同じ状態変数へ反映
- GUI描画: 20Hz timerで外部座標表示を更新
- 音声処理: 最新のXY/touch状態をaudio callback内で参照
- 制御値: 25msの線形スムージングを通して音声パラメータへ渡す
- 安全解除: UDPサンプルが200ms届かなければtouchを強制解除

オーディオcallback内ではmutex待ちや動的メモリ確保を行わない。複数FXで増えたピークは、通常音量を変えない0.95以上だけのソフトリミットで保護する。

## 書道向けプリセット

プリセット欄は3つのFX割当をまとめて切り替える。個別のFXを変更すると `Custom` になる。

| プリセット | X | Y | Pressure | 使いどころ |
| --- | --- | --- | --- | --- |
| `Ink Accent` | Peak EQ | Off | Off | 横移動で強調帯域、筆圧でブースト量。文字の輪郭を保ちやすい基本形。 |
| `Ink Bleed` | Ladder | Off | Reverb | 払いで音色を開き、押し込みで空間を深くする。 |
| `Rhythm Strokes` | Filter | Off | Gate | 筆運びでDJフィルター、強い止め・点で拍のゲートを強調。 |
| `Sweep Calligraphy` | Filter | Peak EQ | Off | 横でフィルター、縦で強調帯域をなぞる。音程感のある線向け。 |
| `Soft Wash` | Low Pass | Off | Reverb | 淡い塗り・にじみのような柔らかい表現。 |
| `Echo Script` | Peak EQ | Off | Echo | 線の位置で音色、筆圧でBPM同期の残響を作る。 |
| `Glitch Dots` | BitCrusher | Off | Roll | 点・跳ねをロールやローファイ音として際立たせる。 |
| `Bass Stroke` | Low Shelf | Ladder | Compressor | 太い横画や力強い漢字向けの低域表現。 |
| `Bright Stroke` | High Shelf | Phaser | Gate | 明るい払いとリズミカルな止めを強調。 |
| `Air Brush` | Chorus | Auto Pan | Reverb | 大きな筆運び用の広がりのある実験的な空間表現。 |

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
| `Peak EQ` | 軸=中心周波数、荷重=ブースト量（筆圧表現向け）。 |
| `Low Shelf` / `High Shelf` | 低域/高域の棚ブースト・カット（JUCE IIR）。荷重でドライミックス。 |
| `Ladder` | 軸=cutoff、荷重=resonance/drive（JUCE Moog風）。 |
| `Flanger` | 軸=rate、荷重=depth/wet。 |
| `Phaser` | 軸=rate/centre、荷重=feedback/mix（JUCE）。 |
| `Chorus` | 軸=rate/delay、荷重=depth/mix（JUCE）。 |
| `Tremolo` | LFO rate、depth。荷重でドライミックス。 |
| `Auto Pan` | LFOで左右パン。荷重でドライミックス。 |
| `Drive` | 軸=キャラ、荷重=ドライブ量。 |
| `BitCrusher` | quantization depth、wet。荷重でドライミックス。 |
| `Compressor` | threshold / ratio / attack / release（JUCE）。荷重でドライミックス。 |
| `Roll` | `1/32` から `1 bar` までのテンポ同期リピート。荷重でドライミックス。 |
| `Off` | その軸にエフェクトを割り当てない。 |

## BPM解析

`TempoAnalyzer` は音源読み込み時に軽量なonset envelopeを作り、自己相関でBPMを推定します。目的は厳密な音楽解析ではなく、FXのテンポ同期に使える安定したBPM状態を得ることです。

- 解析範囲: 先頭から最大600秒
- hop size: 1024 samples
- 特徴量: channel RMS energyのフレーム差分
- 探索範囲: 55.0..210.0 BPM、0.5 BPM刻み
- 全体BPM: 比較区間長に左右されない正規化自己相関の最大値
- ローカルBPM: 24秒window / 4秒step
- 半分テンポ補正: 70 BPMと140 BPMのような候補が近い場合、テンポ同期FXに自然な90..190 BPM帯を優先（倍テンポの一致度が72%以上の場合のみ）
- 安定化: 全体BPM、倍テンへの吸着 + 3点median filter
- セクション化: 6 BPM未満、1秒未満の変化を無視
- 変更回数: 1曲あたり最大2回
- 適用: 同じ候補が1秒以上続いた場合のみBPM状態を切り替え

この設計は、遅いイントロから一定テンポの本編へ移るような曲を想定しています。細かいBPM揺れを追うのではなく、FX操作に使いやすいテンポ状態へ丸めます。

## Nightcoreモード

Nightcoreモードは Track（ファイル再生）時のみ有効です。`AudioTransportSource` のsource sample-rate correctionを `1.18x` にし、ピッチとテンポを同時に上げます。

```text
12 * log2(1.18) ~= +2.9 semitones
```

切り替え時は元音源上の再生位置を維持します。BPM表示とテンポ同期エフェクトには、再生倍率を反映したBPMを使います。
