#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

/** XYパッドの各軸に割り当て可能なエフェクト種類。
    ComboBox の item ID としても使うため、既存値の番号は変えないこと。
    off は 0 を避け（JUCE ComboBox の「未選択」と衝突するため）100 を使う。
*/
enum class EffectType
{
    gate = 1,
    echo,
    reverb,
    filter,
    lowPass,
    highPass,
    bandPass,
    notch,
    flanger,
    phaser,
    tremolo,
    drive,
    bitCrusher,
    roll,
    // --- ここから JUCE dsp モジュールを活かした追加エフェクト ---
    chorus,
    compressor,
    peakEq,
    lowShelf,
    highShelf,
    ladder,
    autoPan,
    off = 100
};

juce::String effectName (EffectType effect);

/** 円形バッファによる簡易ディレイ線。
    Echo / Flanger / Roll が「過去の音」を読み出すために使う。
*/
struct DelayBuffer
{
    juce::AudioBuffer<float> buffer;
    int writePosition = 0;

    void prepare (double sampleRate, double seconds);
    void clear();
    float read (int channel, int delaySamples) const;
    float readFractional (int channel, float delaySamples) const;
    void write (int channel, float value);
    void advance();
};

/** 1軸分のエフェクト処理。
    X軸用・Y軸用でインスタンスを2つ持ち、パッド押下中だけ process() が呼ばれる。
*/
class EffectProcessor
{
public:
    void prepare (int samplesPerBlockExpected, double sampleRate);
    void resetAll();
    void resetMomentary();
    void writeRollHistory (const juce::AudioSourceChannelInfo& info);
    /** amount=軸位置(0..1), pressure=筆圧/荷重(0..1)。マウス操作時は pressure=1 を渡す。 */
    void process (const juce::AudioSourceChannelInfo& info, EffectType effect,
                  float amount, float bpm, float pressure);

private:
    static float smoothstep (float edge0, float edge1, float value);
    static float pickSteppedValue (float x, const std::vector<float>& values);

    void applyGate (const juce::AudioSourceChannelInfo& info, float amount, float bpm, float pressure);
    void applyEcho (const juce::AudioSourceChannelInfo& info, float amount, float bpm, float pressure);
    void applyReverb (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyFilter (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyLowPass (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyHighPass (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyBandPass (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyNotch (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyFlanger (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyPhaser (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyTremolo (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyDrive (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyBitCrusher (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyRoll (const juce::AudioSourceChannelInfo& info, float amount, float bpm, float pressure);
    void applyChorus (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyCompressor (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyPeakEq (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyLowShelf (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyHighShelf (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyLadder (const juce::AudioSourceChannelInfo& info, float amount, float pressure);
    void applyAutoPan (const juce::AudioSourceChannelInfo& info, float amount, float pressure);

    /** エフェクト適用後のバッファを、pressure に応じて原音と混ぜる（筆圧＝効きの深さ）。 */
    void mixWithDry (const juce::AudioSourceChannelInfo& info,
                     const juce::AudioBuffer<float>& dry,
                     float pressure);

    // ステレオ用に左右それぞれ IIR を持つラッパ
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> filter
        { juce::dsp::IIR::Coefficients<float>::makeAllPass (44100.0, 1000.0) };

    juce::dsp::Reverb reverb;
    juce::dsp::Phaser<float> phaser;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::LadderFilter<float> ladderFilter;

    DelayBuffer echoDelay;
    DelayBuffer flangerDelay;
    DelayBuffer rollBuffer;
    juce::AudioBuffer<float> dryBuffer;

    double currentSampleRate = 0.0;
    float gatePhase = 0.0f;
    float flangerPhase = 0.0f;
    float tremoloPhase = 0.0f;
    float autoPanPhase = 0.0f;
    int rollReadOffset = 0;
};
