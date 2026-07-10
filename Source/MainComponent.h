#pragma once

#include "ControllerReceiver.h"
#include "Effects.h"
#include "Tempo.h"
#include "XYPad.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>

class MainComponent final : public juce::AudioAppComponent,
                            private juce::Timer,
                            private juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;

private:
    void setupComboBoxes();
    void refreshAudioFiles();
    void loadSelectedAudioFile();
    juce::Array<juce::File> getAudioDirectories() const;
    void timerCallback() override;
    void updateModeButton();
    void updatePadLabels();
    void updateBpmLabel();
    void updatePositionDisplay();
    void updatePlayButton();
    void updateNightcoreButton();
    void updateControllerPadDisplay();
    void updateStatus();
    void handleControllerSample (const ControllerSample& sample);
    void setControllerStatus (juce::String message);
    void togglePlayback();
    void toggleNightcore();
    void reconnectTransportSource (double sourcePositionToRestore);
    void setPlaybackWanted (bool shouldPlay);
    double getPlaybackRateMultiplier() const;
    float getMusicalBpm() const;
    static juce::String formatTime (double seconds);

    XYPad pad;
    juce::Label title;
    juce::TextButton modeButton;
    juce::TextButton playButton;
    juce::TextButton nightcoreButton;
    juce::ComboBox fileBox;
    juce::ComboBox xEffectBox;
    juce::ComboBox yEffectBox;
    juce::Slider positionSlider;
    juce::Label bpmLabel;
    juce::Label positionLabel;
    juce::Label status;

    juce::String loadedFileName;
    juce::String controllerStatus = "Controller: waiting";
    juce::Array<juce::File> audioFiles;
    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    double loadedSourceSampleRate = 0.0;

    EffectProcessor xEffectProcessor;
    EffectProcessor yEffectProcessor;
    ControllerReceiver controllerReceiver { 45454 };
    TempoAnalysis tempoAnalysis;

    juce::CriticalSection stateLock;
    EffectType xEffect = EffectType::filter;
    EffectType yEffect = EffectType::echo;
    float padX = 0.5f;
    float padY = 0.0f;
    bool effectsActive = false;
    bool lastPointerDown = false;

    std::atomic<bool> shouldResetMomentaryState { false };
    std::atomic<bool> useExternalInput { false };
    std::atomic<bool> userIsSeeking { false };
    std::atomic<bool> shouldBePlaying { true };
    std::atomic<bool> nightcoreEnabled { false };
    std::atomic<float> currentBpm { 120.0f };
    float pendingBpm = 120.0f;
    int pendingBpmTicks = 0;
    std::atomic<float> controllerGuiX { 0.5f };
    std::atomic<float> controllerGuiY { 0.0f };
    std::atomic<bool> controllerGuiTouching { false };
    std::atomic<bool> controllerGuiDirty { false };
};
