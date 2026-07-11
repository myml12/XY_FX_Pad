#include "Effects.h"

#include <algorithm>
#include <cmath>

//==============================================================================
// このファイルの読み方（初心者向け）
//
// ・amount   … XYパッドの軸の値 (0.0〜1.0)。位置・カットオフ・ディレイ長など。
// ・pressure … 筆圧/合計荷重 (0.0〜1.0)。効きの深さ・EQゲイン・レゾナンスなど。
// ・bpm      … 曲のテンポ。Gate / Echo / Roll など「拍に合わせる」系で使う。
// ・info     … 今処理している音声バッファ（ステレオの短い塊）。
// ・wet      … エフェクト音の混ぜ具合。0=原音のみ、1=エフェクト音寄り。
// ・cutoff   … フィルタが効き始める周波数 (Hz)。低い=暗い、高い=明るい。
//
// 荷重の使い方（ざっくり）:
//   Peak EQ / Ladder など … 軸=音色の位置、荷重=強さ（ゲインやレゾナンス）
//   それ以外               … エフェクトをかけたあと、荷重で原音とウェットを混ぜる
//
// JUCE の dsp モジュールを使うもの:
//   Reverb / Phaser / Chorus / Compressor / LadderFilter / IIR Filter
// 自前でサンプルごとに計算するもの:
//   Gate / Echo / Flanger / Tremolo / Drive / BitCrusher / Roll / AutoPan(LFO部)
//==============================================================================

namespace
{
    /** info が指す区間だけを取り出し、JUCE の process() に渡せる形にする。 */
    template <typename Processor>
    void runProcessor (const juce::AudioSourceChannelInfo& info, Processor& processor)
    {
        auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                         .getSubBlock (static_cast<size_t> (info.startSample),
                                       static_cast<size_t> (info.numSamples));
        juce::dsp::ProcessContextReplacing<float> context (block);
        processor.process (context);
    }
}

juce::String effectName (EffectType effect)
{
    switch (effect)
    {
        case EffectType::gate:       return "Gate";
        case EffectType::echo:       return "Echo";
        case EffectType::reverb:     return "Reverb";
        case EffectType::filter:     return "Filter";
        case EffectType::lowPass:    return "Low Pass";
        case EffectType::highPass:   return "High Pass";
        case EffectType::bandPass:   return "Band Pass";
        case EffectType::notch:      return "Notch";
        case EffectType::flanger:    return "Flanger";
        case EffectType::phaser:     return "Phaser";
        case EffectType::tremolo:    return "Tremolo";
        case EffectType::drive:      return "Drive";
        case EffectType::bitCrusher: return "BitCrusher";
        case EffectType::roll:       return "Roll";
        case EffectType::chorus:     return "Chorus";
        case EffectType::compressor: return "Compressor";
        case EffectType::peakEq:     return "Peak EQ";
        case EffectType::lowShelf:   return "Low Shelf";
        case EffectType::highShelf:  return "High Shelf";
        case EffectType::ladder:     return "Ladder";
        case EffectType::autoPan:    return "Auto Pan";
        case EffectType::off:        return "Off";
    }

    return "Unknown";
}

//==============================================================================
// DelayBuffer … 過去のサンプルを円形に保存する「テープ」のようなもの
//==============================================================================

void DelayBuffer::prepare (double sampleRate, double seconds)
{
    // sampleRate * seconds 個ぶんのメモリを確保（例: 44100Hz × 2秒 ≈ 88200）
    buffer.setSize (2, juce::jmax (2, static_cast<int> (sampleRate * seconds)));
    clear();
}

void DelayBuffer::clear()
{
    buffer.clear();
    writePosition = 0;
}

float DelayBuffer::read (int channel, int delaySamples) const
{
    // 「今書いている位置」から delaySamples 個前を読む
    const auto size = buffer.getNumSamples();
    const auto readPosition = (writePosition + size - juce::jlimit (1, size - 1, delaySamples)) % size;
    return buffer.getSample (juce::jmin (channel, buffer.getNumChannels() - 1), readPosition);
}

float DelayBuffer::readFractional (int channel, float delaySamples) const
{
    // 小数サンプルの遅延 → 隣り合う2点を線形補間（Flanger の滑らかな揺れ用）
    const auto size = buffer.getNumSamples();
    auto readPosition = static_cast<float> (writePosition)
                        - juce::jlimit (1.0f, static_cast<float> (size - 2), delaySamples);

    while (readPosition < 0.0f)
        readPosition += static_cast<float> (size);

    const auto index0 = static_cast<int> (readPosition) % size;
    const auto index1 = (index0 + 1) % size;
    const auto frac = readPosition - static_cast<float> (index0);
    const auto safeChannel = juce::jmin (channel, buffer.getNumChannels() - 1);
    return juce::jmap (frac, buffer.getSample (safeChannel, index0), buffer.getSample (safeChannel, index1));
}

void DelayBuffer::write (int channel, float value)
{
    buffer.setSample (juce::jmin (channel, buffer.getNumChannels() - 1), writePosition, value);
}

void DelayBuffer::advance()
{
    // 書き込みヘッドを1サンプル進める（末尾の次は先頭へ戻る）
    writePosition = (writePosition + 1) % buffer.getNumSamples();
}

//==============================================================================
// EffectProcessor の準備・リセット
//==============================================================================

void EffectProcessor::prepare (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;

    // JUCE dsp プロセッサに「サンプリングレート・ブロックサイズ・チャンネル数」を教える
    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlockExpected), 2 };
    reverb.prepare (spec);
    phaser.prepare (spec);
    chorus.prepare (spec);
    compressor.prepare (spec);
    ladderFilter.prepare (spec);
    filter.reset();
    filter.prepare (spec);

    // 自前ディレイ線の長さ（秒）
    echoDelay.prepare (sampleRate, 2.2);      // Echo 用（最大約2拍ぶん余裕）
    flangerDelay.prepare (sampleRate, 0.08);  // Flanger 用（数ms〜十数ms）
    rollBuffer.prepare (sampleRate, 8.0);     // Roll 用（長いループ履歴）
    dryBuffer.setSize (2, samplesPerBlockExpected, false, false, true);

    ladderFilter.setMode (juce::dsp::LadderFilterMode::LPF24); // Moog風 24dB/oct LPF
    resetAll();
}

void EffectProcessor::resetAll()
{
    echoDelay.clear();
    flangerDelay.clear();
    rollBuffer.clear();
    reverb.reset();
    phaser.reset();
    chorus.reset();
    compressor.reset();
    ladderFilter.reset();
    gatePhase = 0.0f;
    flangerPhase = 0.0f;
    tremoloPhase = 0.0f;
    autoPanPhase = 0.0f;
    rollReadOffset = 0;
}

void EffectProcessor::resetMomentary()
{
    // パッドを押し直したとき用。履歴が残ると気持ち悪い系だけクリア。
    echoDelay.clear();
    reverb.reset();
    phaser.reset();
    chorus.reset();
    compressor.reset();
    gatePhase = 0.0f;
    tremoloPhase = 0.0f;
    autoPanPhase = 0.0f;
    rollReadOffset = 0;
}

void EffectProcessor::writeRollHistory (const juce::AudioSourceChannelInfo& info)
{
    // パッドを押していなくても常に履歴を書き込む。
    // → 押した瞬間から「直前の音」をループ再生できる。
    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), rollBuffer.buffer.getNumChannels()); ++channel)
            rollBuffer.write (channel, info.buffer->getSample (channel, info.startSample + sample));

        rollBuffer.advance();
    }
}

void EffectProcessor::process (const juce::AudioSourceChannelInfo& info, EffectType effect,
                               float amount, float bpm, float pressure)
{
    if (effect == EffectType::off || info.buffer == nullptr || info.numSamples <= 0)
        return;

    bpm = juce::jlimit (70.0f, 190.0f, bpm);
    pressure = juce::jlimit (0.0f, 1.0f, pressure);
    amount = juce::jlimit (0.0f, 1.0f, amount);

    // 荷重を「第2パラメータ」として使う系は、後段のドライミックスをしない
    const bool pressureDrivesIntensity = effect == EffectType::peakEq
                                      || effect == EffectType::ladder
                                      || effect == EffectType::drive
                                      || effect == EffectType::echo
                                      || effect == EffectType::reverb
                                      || effect == EffectType::chorus
                                      || effect == EffectType::flanger
                                      || effect == EffectType::phaser;

    const auto useDryMix = ! pressureDrivesIntensity && pressure < 0.995f
                        && info.buffer->getNumChannels() <= dryBuffer.getNumChannels()
                        && info.numSamples <= dryBuffer.getNumSamples();

    if (useDryMix)
    {
        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            dryBuffer.copyFrom (channel, 0, *info.buffer, channel, info.startSample, info.numSamples);
    }

    switch (effect)
    {
        case EffectType::gate:       applyGate (info, amount, bpm, pressure); break;
        case EffectType::echo:       applyEcho (info, amount, bpm, pressure); break;
        case EffectType::reverb:     applyReverb (info, amount, pressure); break;
        case EffectType::filter:     applyFilter (info, amount, pressure); break;
        case EffectType::lowPass:    applyLowPass (info, amount, pressure); break;
        case EffectType::highPass:   applyHighPass (info, amount, pressure); break;
        case EffectType::bandPass:   applyBandPass (info, amount, pressure); break;
        case EffectType::notch:      applyNotch (info, amount, pressure); break;
        case EffectType::flanger:    applyFlanger (info, amount, pressure); break;
        case EffectType::phaser:     applyPhaser (info, amount, pressure); break;
        case EffectType::tremolo:    applyTremolo (info, amount, pressure); break;
        case EffectType::drive:      applyDrive (info, amount, pressure); break;
        case EffectType::bitCrusher: applyBitCrusher (info, amount, pressure); break;
        case EffectType::roll:       applyRoll (info, amount, bpm, pressure); break;
        case EffectType::chorus:     applyChorus (info, amount, pressure); break;
        case EffectType::compressor: applyCompressor (info, amount, pressure); break;
        case EffectType::peakEq:     applyPeakEq (info, amount, pressure); break;
        case EffectType::lowShelf:   applyLowShelf (info, amount, pressure); break;
        case EffectType::highShelf:  applyHighShelf (info, amount, pressure); break;
        case EffectType::ladder:     applyLadder (info, amount, pressure); break;
        case EffectType::autoPan:    applyAutoPan (info, amount, pressure); break;
        case EffectType::off:        break;
    }

    if (useDryMix)
        mixWithDry (info, dryBuffer, pressure);
}

void EffectProcessor::mixWithDry (const juce::AudioSourceChannelInfo& info,
                                  const juce::AudioBuffer<float>& dry,
                                  float pressure)
{
    // 軽いタッチほど原音寄り、強く押すほど FX 全開
    const auto wet = smoothstep (0.05f, 0.85f, pressure);

    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
    {
        auto* out = info.buffer->getWritePointer (channel, info.startSample);
        const auto* dryData = dry.getReadPointer (channel);

        for (int sample = 0; sample < info.numSamples; ++sample)
            out[sample] = dryData[sample] * (1.0f - wet) + out[sample] * wet;
    }
}

//==============================================================================
// 共通ユーティリティ
//==============================================================================

float EffectProcessor::smoothstep (float edge0, float edge1, float value)
{
    // 0〜1 を滑らかに立ち上げる曲線（端でカクつかない）
    const auto x = juce::jlimit (0.0f, 1.0f, (value - edge0) / (edge1 - edge0));
    return x * x * (3.0f - 2.0f * x);
}

float EffectProcessor::pickSteppedValue (float x, const std::vector<float>& values)
{
    // 連続の amount を「段階値」に丸める（例: Echo の音符長）
    const auto index = juce::jlimit (0, static_cast<int> (values.size()) - 1,
                                     static_cast<int> (std::floor (x * static_cast<float> (values.size()))));
    return values[static_cast<size_t> (index)];
}

//==============================================================================
// Gate … 16分音符のリズムで音量をオン/オフ（トランサー風）
//==============================================================================

void EffectProcessor::applyGate (const juce::AudioSourceChannelInfo& info, float amount, float bpm, float pressure)
{
    juce::ignoreUnused (pressure); // 効きの深さは process() 側のドライミックスで制御
    const auto periodBeats = 0.25f; // 1拍の 1/4 = 16分音符
    const auto periodSeconds = 60.0f / bpm * periodBeats;
    const auto phaseDelta = 1.0f / static_cast<float> (currentSampleRate * periodSeconds);

    const auto response = smoothstep (0.0f, 0.8f, amount);
    const auto wet = juce::jmap (response, 0.0f, 1.0f, 0.18f, 1.0f);       // 効きの強さ
    const auto floorGain = juce::jmap (response, 0.0f, 1.0f, 0.48f, 0.02f); // 閉じたときの残り音量
    const auto duty = juce::jmap (response, 0.0f, 1.0f, 0.68f, 0.34f);      // 開いている時間の割合
    const auto edge = 0.025f; // 開閉のフェード幅（クリック防止）

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        gatePhase += phaseDelta;
        if (gatePhase >= 1.0f)
            gatePhase -= 1.0f;

        // 位相 0〜duty で開き、それ以外で閉じる台形エンベロープ
        const auto openEdge = smoothstep (0.0f, edge, gatePhase);
        const auto closeEdge = 1.0f - smoothstep (duty, duty + edge, gatePhase);
        const auto gate = floorGain + (1.0f - floorGain) * juce::jmin (openEdge, closeEdge);
        const auto gain = juce::jmap (wet, 1.0f, gate);

        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            info.buffer->getWritePointer (channel, info.startSample)[sample] *= gain;
    }
}

//==============================================================================
// Echo … BPM同期ディレイ（やまびこ）
//==============================================================================

void EffectProcessor::applyEcho (const juce::AudioSourceChannelInfo& info, float amount, float bpm, float pressure)
{
    // 軸 = ディレイ時間（音符）、荷重 = フィードバックとウェットの深さ
    const auto delayBeats = pickSteppedValue (amount, { 0.0625f, 0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 2.0f });
    const auto delaySamples = static_cast<int> ((60.0f / bpm) * delayBeats * currentSampleRate);
    const auto depth = smoothstep (0.05f, 0.9f, pressure);
    const auto feedback = juce::jmap (depth, 0.0f, 1.0f, 0.05f, juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.25f, 0.82f));
    const auto wet = juce::jmap (depth, 0.0f, 1.0f, 0.08f, juce::jmap (amount, 0.0f, 1.0f, 0.35f, 0.72f));

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), echoDelay.buffer.getNumChannels()); ++channel)
        {
            auto* data = info.buffer->getWritePointer (channel, info.startSample);
            const auto input = data[sample];
            const auto delayed = echoDelay.read (channel, delaySamples);

            // 原音を少し下げつつ、遅延音を混ぜる
            data[sample] = input * (1.0f - wet * 0.35f) + delayed * wet;

            // フィードバック: 遅延音の一部を再びディレイに戻す → 繰り返し
            echoDelay.write (channel, input + delayed * feedback);
        }

        echoDelay.advance();
    }
}

//==============================================================================
// Reverb … JUCE 内蔵リバーブ（部屋の残響）
//==============================================================================

void EffectProcessor::applyReverb (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    // 軸 = 部屋の大きさ、荷重 = ウェット量（軽く撫でるだけなら薄く残響）
    const auto depth = smoothstep (0.05f, 0.9f, pressure);
    juce::dsp::Reverb::Parameters params;
    params.roomSize = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.18f, 0.98f);
    params.damping = juce::jmap (amount, 0.0f, 1.0f, 0.62f, 0.18f);
    params.wetLevel = juce::jmap (depth, 0.0f, 1.0f, 0.0f, juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.2f, 0.68f));
    params.dryLevel = 1.0f - params.wetLevel * 0.35f;
    params.width = 1.0f;
    reverb.setParameters (params);

    runProcessor (info, reverb);
}

//==============================================================================
// Filter … 1軸で LPF ↔ 素通し ↔ HPF をモーフィング（DJの「フィルター」定番）
//==============================================================================

void EffectProcessor::applyFilter (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    const auto q = 2.6f; // 共振（カットオフ付近のピーク）

    if (amount < 0.48f)
    {
        // 左寄り: ローパス（暗い方向へ）
        const auto normalised = juce::jmap (amount, 0.0f, 0.48f, 0.0f, 1.0f);
        const auto cutoff = juce::jmap (normalised * normalised, 90.0f, 19000.0f);
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, cutoff, q);
    }
    else if (amount > 0.52f)
    {
        // 右寄り: ハイパス（薄い／シャリシャリ方向へ）
        const auto normalised = juce::jmap (amount, 0.52f, 1.0f, 0.0f, 1.0f);
        const auto cutoff = juce::jmap (normalised * normalised, 25.0f, 7600.0f);
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, cutoff, q);
    }
    else
    {
        // 中央付近: ほぼ素通し（オールパス）
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeAllPass (currentSampleRate, 1000.0f);
    }

    runProcessor (info, filter);
}

//==============================================================================
// Low Pass … 高い周波数をカット（こもった音に）
//==============================================================================

void EffectProcessor::applyLowPass (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    // amount↑ → カットオフ↑ → より多くの高域が通る（明るくなる）
    const auto cutoff = juce::jmap (amount * amount, 180.0f, 19000.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, cutoff, 1.25f);
    runProcessor (info, filter);
}

//==============================================================================
// High Pass … 低い周波数をカット（薄く・軽く）
//==============================================================================

void EffectProcessor::applyHighPass (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    // amount↑ → カットオフ↑ → 低域がより削られる
    const auto cutoff = juce::jmap (amount * amount, 24.0f, 8200.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, cutoff, 1.25f);
    runProcessor (info, filter);
}

//==============================================================================
// Band Pass … 特定帯域だけ通す（電話声〜ワウっぽい）
//==============================================================================

void EffectProcessor::applyBandPass (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    const auto cutoff = juce::jmap (amount * amount, 120.0f, 12000.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, cutoff, 1.25f);
    runProcessor (info, filter);

    // バンドパスはエネルギーが減りやすいので少しゲイン補正
    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
        info.buffer->applyGain (channel, info.startSample, info.numSamples, 2.2f);
}

//==============================================================================
// Notch … 特定帯域だけ削る（ハウリング抑制や「穴が空いた」音）
//==============================================================================

void EffectProcessor::applyNotch (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    const auto cutoff = juce::jmap (amount * amount, 140.0f, 12000.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeNotch (currentSampleRate, cutoff, 1.1f);
    runProcessor (info, filter);
}

//==============================================================================
// Flanger … ごく短いディレイを LFO で揺らして金属的なうねりを作る
//==============================================================================

void EffectProcessor::applyFlanger (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    // 軸 = 変調レート、荷重 = 深さ・ウェット
    const auto depth = smoothstep (0.05f, 0.9f, pressure);
    const auto rate = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.08f, 6.5f);
    const auto depthMs = juce::jmap (depth, 0.0f, 1.0f, 0.3f, juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 2.0f, 9.5f));
    const auto baseMs = 2.0f;
    const auto wet = juce::jmap (depth, 0.0f, 1.0f, 0.1f, juce::jmap (amount, 0.0f, 1.0f, 0.4f, 0.78f));

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        flangerPhase += juce::MathConstants<float>::twoPi * rate / static_cast<float> (currentSampleRate);
        if (flangerPhase >= juce::MathConstants<float>::twoPi)
            flangerPhase -= juce::MathConstants<float>::twoPi;

        // sin で遅延時間を往復させる
        const auto delaySamples = (baseMs + depthMs * (0.5f + 0.5f * std::sin (flangerPhase)))
                                  * 0.001f * static_cast<float> (currentSampleRate);

        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), flangerDelay.buffer.getNumChannels()); ++channel)
        {
            auto* data = info.buffer->getWritePointer (channel, info.startSample);
            const auto input = data[sample];
            const auto delayed = flangerDelay.readFractional (channel, delaySamples);
            data[sample] = input * (1.0f - wet * 0.35f) + delayed * wet;
            flangerDelay.write (channel, input + delayed * 0.42f); // 軽いフィードバック
        }

        flangerDelay.advance();
    }
}

//==============================================================================
// Phaser … JUCE Phaser（オールパスを並べて位相をずらし、うねるノッチを作る）
//==============================================================================

void EffectProcessor::applyPhaser (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    // 軸 = レート／中心周波数、荷重 = フィードバックとミックス
    const auto depth = smoothstep (0.05f, 0.9f, pressure);
    phaser.setRate (juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.12f, 9.0f));
    phaser.setDepth (juce::jmap (depth, 0.0f, 1.0f, 0.25f, 0.95f));
    phaser.setCentreFrequency (juce::jmap (amount, 0.0f, 1.0f, 700.0f, 1800.0f));
    phaser.setFeedback (juce::jmap (depth, 0.0f, 1.0f, 0.0f, juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.2f, 0.92f)));
    phaser.setMix (juce::jmap (depth, 0.0f, 1.0f, 0.15f, 0.78f));
    runProcessor (info, phaser);
}

//==============================================================================
// Tremolo … 音量を LFO で上下（ワウワウというより「点滅」）
//==============================================================================

void EffectProcessor::applyTremolo (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    const auto depth = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.0f, 0.96f);
    const auto rate = juce::jmap (amount * amount, 0.0f, 1.0f, 0.75f, 18.0f);
    const auto phaseDelta = juce::MathConstants<float>::twoPi * rate / static_cast<float> (currentSampleRate);

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        // sin が -1〜1 → 0〜1 のゲインに変換し、depth で深さ調整
        const auto gain = 1.0f - depth * (0.5f + 0.5f * std::sin (tremoloPhase));
        tremoloPhase += phaseDelta;

        if (tremoloPhase >= juce::MathConstants<float>::twoPi)
            tremoloPhase -= juce::MathConstants<float>::twoPi;

        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            info.buffer->getWritePointer (channel, info.startSample)[sample] *= gain;
    }
}

//==============================================================================
// Drive … tanh によるソフトクリッピング歪み
//==============================================================================

void EffectProcessor::applyDrive (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    // 軸 = 歪みキャラの位置感、荷重 = 実際のドライブ量（筆圧で歪む）
    const auto depth = smoothstep (0.05f, 0.9f, pressure);
    const auto drive = juce::jmap (depth, 0.0f, 1.0f, 1.0f, juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 2.0f, 7.5f));
    const auto wet = juce::jmap (depth, 0.0f, 1.0f, 0.0f, juce::jmap (amount, 0.0f, 1.0f, 0.35f, 0.72f));
    const auto outputTrim = juce::jmap (depth, 0.0f, 1.0f, 0.96f, 0.68f);

    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
    {
        auto* data = info.buffer->getWritePointer (channel, info.startSample);

        for (int sample = 0; sample < info.numSamples; ++sample)
        {
            const auto input = data[sample];
            // tanh は大きな入力を ±1 付近に丸める → 暖かい歪み
            const auto driven = std::tanh (input * drive) * outputTrim;
            data[sample] = juce::jlimit (-0.98f, 0.98f, juce::jmap (wet, input, driven));
        }
    }
}

//==============================================================================
// BitCrusher … 量子化ステップを粗くしてローファイ感を出す
//==============================================================================

void EffectProcessor::applyBitCrusher (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    // amount↑ → steps↓ → 段階が粗く → ザラザラ
    const auto steps = juce::jmap (1.0f - smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 4.0f, 512.0f);
    const auto wet = juce::jlimit (0.0f, 1.0f, smoothstep (0.0f, 0.85f, amount));

    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
    {
        auto* data = info.buffer->getWritePointer (channel, info.startSample);

        for (int sample = 0; sample < info.numSamples; ++sample)
        {
            const auto input = data[sample];
            const auto crushed = std::round (input * steps) / steps; // 階段状に丸める
            data[sample] = juce::jmap (wet, input, crushed);
        }
    }
}

//==============================================================================
// Roll … 直近の短い区間をループ再生（DJのロール／スタッター）
//==============================================================================

void EffectProcessor::applyRoll (const juce::AudioSourceChannelInfo& info, float amount, float bpm, float pressure)
{
    juce::ignoreUnused (pressure);
    // amount でループ長を段階選択（短い連打 → 長いループ）
    static constexpr std::array<float, 6> beatLengths { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    const auto index = juce::jlimit (0, static_cast<int> (beatLengths.size()) - 1,
                                     static_cast<int> (std::floor (amount * static_cast<float> (beatLengths.size()))));
    const auto beats = beatLengths[static_cast<size_t> (index)];
    const auto seconds = 60.0f / bpm * beats;
    const auto segmentSamples = juce::jlimit (64, rollBuffer.buffer.getNumSamples() - 2,
                                              static_cast<int> (seconds * currentSampleRate));
    const auto wet = juce::jlimit (0.0f, 1.0f, smoothstep (0.0f, 0.85f, amount));

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        // セグメント内を循環する読み位置
        const auto delay = segmentSamples - (rollReadOffset % segmentSamples);

        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), rollBuffer.buffer.getNumChannels()); ++channel)
        {
            auto* data = info.buffer->getWritePointer (channel, info.startSample);
            const auto input = data[sample];
            const auto rolled = rollBuffer.read (channel, delay);
            data[sample] = juce::jmap (wet, input, rolled);
        }

        ++rollReadOffset;
    }
}

//==============================================================================
// Chorus … JUCE Chorus（少し長めの変調ディレイで厚み・揺らぎ）
//==============================================================================

void EffectProcessor::applyChorus (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    // 軸 = レート／ディレイ、荷重 = 深さ・ミックス
    const auto t = smoothstep (0.0f, 1.0f, amount);
    const auto depth = smoothstep (0.05f, 0.9f, pressure);
    chorus.setRate (juce::jmap (t, 0.0f, 1.0f, 0.15f, 3.5f));
    chorus.setDepth (juce::jmap (depth, 0.0f, 1.0f, 0.08f, juce::jmap (t, 0.0f, 1.0f, 0.25f, 0.85f)));
    chorus.setCentreDelay (juce::jmap (t, 0.0f, 1.0f, 7.0f, 18.0f));
    chorus.setFeedback (juce::jmap (depth, 0.0f, 1.0f, 0.0f, 0.35f));
    chorus.setMix (juce::jmap (depth, 0.0f, 1.0f, 0.1f, juce::jmap (amount, 0.0f, 1.0f, 0.35f, 0.85f)));
    runProcessor (info, chorus);
}

//==============================================================================
// Compressor … JUCE Compressor（大きい音を抑え、密度を上げる）
//==============================================================================

void EffectProcessor::applyCompressor (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    const auto t = smoothstep (0.0f, 1.0f, amount);
    // amount↑ → しきい値を下げ・レシオを上げ → より強く潰す
    compressor.setThreshold (juce::jmap (t, 0.0f, 1.0f, -6.0f, -32.0f)); // dB
    compressor.setRatio (juce::jmap (t, 0.0f, 1.0f, 1.5f, 12.0f));
    compressor.setAttack (juce::jmap (t, 0.0f, 1.0f, 20.0f, 2.0f));   // ms（強いほど速く掴む）
    compressor.setRelease (juce::jmap (t, 0.0f, 1.0f, 120.0f, 40.0f)); // ms
    runProcessor (info, compressor);

    // 潰した分だけ全体が小さくなるので、軽いメイクアップゲイン
    const auto makeup = juce::Decibels::decibelsToGain (juce::jmap (t, 0.0f, 1.0f, 0.0f, 6.0f));
    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
        info.buffer->applyGain (channel, info.startSample, info.numSamples, makeup);
}

//==============================================================================
// Peak EQ … 中心周波数をスイープするピーク（ベル）ブースト
//==============================================================================

void EffectProcessor::applyPeakEq (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    // 筆でなぞる用途向け: 軸 = 中心周波数、荷重 = ブースト量
    // 軽く撫でる → ほのかなピーク、強く押す → はっきり持ち上がる
    const auto freq = juce::jmap (amount * amount, 120.0f, 8000.0f);
    const auto gainDb = juce::jmap (smoothstep (0.05f, 0.9f, pressure), 0.0f, 1.0f, 0.5f, 14.0f);
    const auto gain = juce::Decibels::decibelsToGain (gainDb);
    const auto q = juce::jmap (smoothstep (0.0f, 1.0f, pressure), 0.0f, 1.0f, 1.2f, 2.4f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq, q, gain);
    runProcessor (info, filter);
}

//==============================================================================
// Low Shelf … 低域全体をブースト／カット（ベースの太さ調整）
//==============================================================================

void EffectProcessor::applyLowShelf (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    // 中央付近でフラット、左でカット、右でブースト
    const auto gainDb = juce::jmap (amount, 0.0f, 1.0f, -12.0f, 12.0f);
    const auto gain = juce::Decibels::decibelsToGain (gainDb);
    const auto cutoff = 250.0f; // この周波数より下を棚状に動かす
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (currentSampleRate, cutoff, 0.707f, gain);
    runProcessor (info, filter);
}

//==============================================================================
// High Shelf … 高域全体をブースト／カット（明るさ調整）
//==============================================================================

void EffectProcessor::applyHighShelf (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    const auto gainDb = juce::jmap (amount, 0.0f, 1.0f, -12.0f, 12.0f);
    const auto gain = juce::Decibels::decibelsToGain (gainDb);
    const auto cutoff = 3500.0f; // この周波数より上を棚状に動かす
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (currentSampleRate, cutoff, 0.707f, gain);
    runProcessor (info, filter);
}

//==============================================================================
// Ladder … JUCE Moog風ラダーLPF（レゾナンス付きの「うねる」ローパス）
//==============================================================================

void EffectProcessor::applyLadder (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    // 軸 = カットオフ、荷重 = レゾナンス／ドライブ（押すほど鳴る）
    const auto t = smoothstep (0.0f, 1.0f, amount);
    const auto depth = smoothstep (0.05f, 0.9f, pressure);
    ladderFilter.setCutoffFrequencyHz (juce::jmap (t * t, 0.0f, 1.0f, 80.0f, 16000.0f));
    ladderFilter.setResonance (juce::jmap (depth, 0.0f, 1.0f, 0.05f, 0.88f));
    ladderFilter.setDrive (juce::jmap (depth, 0.0f, 1.0f, 1.0f, 2.8f));
    ladderFilter.setEnabled (true);
    runProcessor (info, ladderFilter);
}

//==============================================================================
// Auto Pan … LFO で左右に振る（JUCE Panner + 自前位相）
//==============================================================================

void EffectProcessor::applyAutoPan (const juce::AudioSourceChannelInfo& info, float amount, float pressure)
{
    juce::ignoreUnused (pressure);
    // ステレオ前提。モノラル入力だと効果は薄い。
    if (info.buffer->getNumChannels() < 2)
        return;

    const auto t = smoothstep (0.0f, 1.0f, amount);
    const auto rate = juce::jmap (amount * amount, 0.0f, 1.0f, 0.2f, 8.0f); // Hz
    const auto depth = juce::jmap (t, 0.0f, 1.0f, 0.15f, 1.0f);              // 振り幅
    const auto phaseDelta = juce::MathConstants<float>::twoPi * rate / static_cast<float> (currentSampleRate);

    auto* left = info.buffer->getWritePointer (0, info.startSample);
    auto* right = info.buffer->getWritePointer (1, info.startSample);

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        // pan = -1(左) 〜 +1(右)。等電力パン（sin/cos）で音量感を保つ
        const auto pan = depth * std::sin (autoPanPhase);
        const auto angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // 0〜π/2
        const auto leftGain = std::cos (angle);
        const auto rightGain = std::sin (angle);

        left[sample] *= leftGain;
        right[sample] *= rightGain;

        autoPanPhase += phaseDelta;
        if (autoPanPhase >= juce::MathConstants<float>::twoPi)
            autoPanPhase -= juce::MathConstants<float>::twoPi;
    }
}
