#include "MainComponent.h"
#include "SystemAudioRouter.h"

MainComponent::MainComponent()
{
    formatManager.registerBasicFormats();

   #if JUCE_MAC
    formatManager.registerFormat (new juce::CoreAudioFormat(), true);
   #endif

    pad.onPadChanged = [this] (float x, float y, bool down)
    {
        audioPadX.store (x);
        audioPadY.store (y);
        audioPadPressure.store (down ? 1.0f : 0.0f); // マウスは常にフル筆圧
        audioEffectsActive.store (down);

        if (lastPointerDown.exchange (down) != down)
            shouldResetMomentaryState.store (true);

        controllerGuiPressure.store (down ? 1.0f : 0.0f);
        pressureMeter.setLevel (down ? 1.0f : 0.0f);
    };

    juce::Component::SafePointer<MainComponent> safeThis (this);
    controllerReceiver.onSample = [safeThis] (const ControllerSample& sample)
    {
        if (safeThis != nullptr)
            safeThis->handleControllerSample (sample);
    };
    controllerReceiver.onConnectionChanged = [safeThis] (bool, juce::String message)
    {
        juce::MessageManager::callAsync ([safeThis, message]
        {
            if (safeThis != nullptr)
                safeThis->setControllerStatus (message);
        });
    };

    addAndMakeVisible (pad);
    addAndMakeVisible (pressureMeter);
    addAndMakeVisible (title);
    addAndMakeVisible (modeButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (nightcoreButton);
    addAndMakeVisible (fileBox);
    addAndMakeVisible (xEffectBox);
    addAndMakeVisible (yEffectBox);
    addAndMakeVisible (pressureEffectBox);
    addAndMakeVisible (performancePresetBox);
    addAndMakeVisible (positionSlider);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (status);

    title.setText ("DJ XY Pad", juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xfff0f2f5));

    modeButton.setButtonText ("Track");
    modeButton.onClick = [this]
    {
        const auto enteringSystemAudioMode = ! useExternalInput.load();

        if (enteringSystemAudioMode)
        {
            if (! applySystemCaptureRouting())
            {
                updateStatus();
                return;
            }

            useExternalInput.store (true);
        }
        else
        {
            useExternalInput.store (false);
            restoreDefaultRouting();
        }

        updateModeButton();
        updateStatus();
    };

    playButton.setButtonText ("Play");
    playButton.onClick = [this] { togglePlayback(); };

    nightcoreButton.setButtonText ("Nightcore");
    nightcoreButton.onClick = [this] { toggleNightcore(); };

    setWantsKeyboardFocus (true);
    addKeyListener (this);
    modeButton.addKeyListener (this);
    playButton.addKeyListener (this);
    nightcoreButton.addKeyListener (this);
    fileBox.addKeyListener (this);
    xEffectBox.addKeyListener (this);
    yEffectBox.addKeyListener (this);
    pressureEffectBox.addKeyListener (this);
    performancePresetBox.addKeyListener (this);
    positionSlider.addKeyListener (this);
    pad.addKeyListener (this);

    setupComboBoxes();
    refreshAudioFiles();

    positionSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    positionSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    positionSlider.setRange (0.0, 1.0, 0.001);
    positionSlider.onDragStart = [this] { userIsSeeking.store (true); };
    positionSlider.onDragEnd = [this]
    {
        if (! useExternalInput.load() && readerSource != nullptr)
            transport.setPosition (positionSlider.getValue());

        userIsSeeking.store (false);
        updatePositionDisplay();
    };
    positionSlider.onValueChange = [this]
    {
        if (userIsSeeking.load() && ! useExternalInput.load() && readerSource != nullptr)
            positionLabel.setText (formatTime (positionSlider.getValue()) + " / "
                                       + formatTime (transport.getLengthInSeconds()),
                                   juce::dontSendNotification);
    };

    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));
    positionLabel.setJustificationType (juce::Justification::centredRight);
    positionLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));
    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));

    const auto styleButton = [] (juce::TextButton& button, juce::Colour accent)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b1e22));
        button.setColour (juce::TextButton::buttonOnColourId, accent);
        button.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.88f));
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    };
    styleButton (modeButton, juce::Colour (0xffc6ef73));
    styleButton (playButton, juce::Colour (0xffc6ef73));
    styleButton (nightcoreButton, juce::Colour (0xffc6ef73));

    const auto styleCombo = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff171a1e));
        box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe9ecef));
        box.setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.10f));
        box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffc6ef73));
    };
    styleCombo (fileBox);
    styleCombo (xEffectBox);
    styleCombo (yEffectBox);
    styleCombo (pressureEffectBox);
    styleCombo (performancePresetBox);
    positionSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff3d4448));
    positionSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffc6ef73));

    loadSelectedAudioFile();
    setSize (760, 820);
    setAudioChannels (0, 2);
    // Track mode is the default: never open BlackHole or leave system output on it.
    applyTrackModeRouting();
    startTimerHz (20);
    updatePlayButton();
    updateNightcoreButton();
    updateStatus();
    controllerReceiver.start();
}

MainComponent::~MainComponent()
{
    stopTimer();
    controllerReceiver.stop();
    pad.removeKeyListener (this);
    positionSlider.removeKeyListener (this);
    performancePresetBox.removeKeyListener (this);
    pressureEffectBox.removeKeyListener (this);
    yEffectBox.removeKeyListener (this);
    xEffectBox.removeKeyListener (this);
    fileBox.removeKeyListener (this);
    nightcoreButton.removeKeyListener (this);
    playButton.removeKeyListener (this);
    modeButton.removeKeyListener (this);
    removeKeyListener (this);
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();

    if (useExternalInput.load())
        restoreDefaultRouting();

    shutdownAudio();
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    transport.prepareToPlay (samplesPerBlockExpected, sampleRate);
    xEffectProcessor.prepare (samplesPerBlockExpected, sampleRate);
    yEffectProcessor.prepare (samplesPerBlockExpected, sampleRate);
    pressureEffectProcessor.prepare (samplesPerBlockExpected, sampleRate);
    smoothedPadX.reset (sampleRate, 0.025);
    smoothedPadY.reset (sampleRate, 0.025);
    smoothedPadPressure.reset (sampleRate, 0.025);
    smoothedPadX.setCurrentAndTargetValue (audioPadX.load());
    smoothedPadY.setCurrentAndTargetValue (audioPadY.load());
    smoothedPadPressure.setCurrentAndTargetValue (audioPadPressure.load());
    effectMix.reset (sampleRate, 0.035);
    effectMix.setCurrentAndTargetValue (audioEffectsActive.load() ? 1.0f : 0.0f);
    dryOutputBuffer.setSize (2, samplesPerBlockExpected, false, false, true);

    if (readerSource != nullptr && shouldBePlaying.load())
        transport.start();
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    const auto inputMode = useExternalInput.load();

    if (! inputMode)
        bufferToFill.clearActiveBufferRegion();

    if (! inputMode && readerSource == nullptr)
        return;

    if (! inputMode)
        transport.getNextAudioBlock (bufferToFill);

    xEffectProcessor.writeRollHistory (bufferToFill);
    yEffectProcessor.writeRollHistory (bufferToFill);
    pressureEffectProcessor.writeRollHistory (bufferToFill);

    const auto active = audioEffectsActive.load();
    smoothedPadX.setTargetValue (audioPadX.load());
    smoothedPadY.setTargetValue (audioPadY.load());
    smoothedPadPressure.setTargetValue (audioPadPressure.load());
    const auto x = smoothedPadX.skip (bufferToFill.numSamples);
    const auto y = smoothedPadY.skip (bufferToFill.numSamples);
    const auto pressure = smoothedPadPressure.skip (bufferToFill.numSamples);
    const auto selectedXEffect = static_cast<EffectType> (audioXEffect.load());
    const auto selectedYEffect = static_cast<EffectType> (audioYEffect.load());
    const auto selectedPressureEffect = static_cast<EffectType> (audioPressureEffect.load());
    effectMix.setTargetValue (active ? 1.0f : 0.0f);
    const auto shouldProcessEffects = active || effectMix.isSmoothing();

    const auto canCrossfade = shouldProcessEffects
                           && bufferToFill.buffer != nullptr
                           && bufferToFill.buffer->getNumChannels() <= dryOutputBuffer.getNumChannels()
                           && bufferToFill.numSamples <= dryOutputBuffer.getNumSamples();

    if (canCrossfade)
        for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
            dryOutputBuffer.copyFrom (channel, 0, *bufferToFill.buffer, channel,
                                      bufferToFill.startSample, bufferToFill.numSamples);

    if (shouldProcessEffects)
    {
        if (active && shouldResetMomentaryState.exchange (false))
        {
            xEffectProcessor.resetMomentary();
            yEffectProcessor.resetMomentary();
            pressureEffectProcessor.resetMomentary();
        }

        const auto bpm = getMusicalBpm();

        // X/Y: 位置が amount、荷重が強さ（Peak EQ などの筆圧表現）
        if (selectedXEffect != EffectType::off)
            xEffectProcessor.process (bufferToFill, selectedXEffect, x, bpm, pressure);

        if (selectedYEffect != EffectType::off)
            yEffectProcessor.process (bufferToFill, selectedYEffect, y, bpm, pressure);

        // 荷重専用エフェクト: amount=筆圧。強さパラメータはフルで渡す
        if (selectedPressureEffect != EffectType::off)
            pressureEffectProcessor.process (bufferToFill, selectedPressureEffect, pressure, bpm, 1.0f);
    }

    if (canCrossfade)
        crossfadeEffectsWithDry (bufferToFill);

    softLimitOutput (bufferToFill);
}

void MainComponent::crossfadeEffectsWithDry (const juce::AudioSourceChannelInfo& bufferToFill)
{
    for (int sample = 0; sample < bufferToFill.numSamples; ++sample)
    {
        const auto wet = effectMix.getNextValue();

        for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
        {
            auto* output = bufferToFill.buffer->getWritePointer (channel, bufferToFill.startSample);
            const auto* dry = dryOutputBuffer.getReadPointer (channel);
            output[sample] = dry[sample] * (1.0f - wet) + output[sample] * wet;
        }
    }
}

void MainComponent::softLimitOutput (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
        return;

    for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
    {
        auto* data = bufferToFill.buffer->getWritePointer (channel, bufferToFill.startSample);

        for (int sample = 0; sample < bufferToFill.numSamples; ++sample)
        {
            const auto value = data[sample];
            const auto magnitude = std::abs (value);

            // 0.95未満は完全に素通し。超過分だけ滑らかに飽和させるため、
            // 通常再生の音量や質感を変えずに、複数FXのピークを保護できる。
            if (magnitude > 0.95f)
                data[sample] = std::copysign (0.95f + 0.05f * std::tanh ((magnitude - 0.95f) / 0.05f), value);
        }
    }
}

void MainComponent::releaseResources()
{
    transport.stop();
    transport.releaseResources();
}

void MainComponent::paint (juce::Graphics& g)
{
    juce::ColourGradient background (juce::Colour (0xff090a0b), 0.0f, 0.0f,
                                     juce::Colour (0xff151719), static_cast<float> (getWidth()),
                                     static_cast<float> (getHeight()), false);
    background.addColour (0.48, juce::Colour (0xff0e1012));
    g.setGradientFill (background);
    g.fillAll();

    auto content = getLocalBounds().reduced (18).toFloat();
    content.removeFromTop (52.0f);
    auto controlsCard = content.removeFromTop (222.0f);
    g.setColour (juce::Colour (0xff121416).withAlpha (0.96f));
    g.fillRoundedRectangle (controlsCard, 16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (controlsCard, 16.0f, 1.0f);

    content.removeFromTop (10.0f);
    auto padCard = content.withTrimmedBottom (42.0f);
    g.setColour (juce::Colour (0xff101214).withAlpha (0.94f));
    g.fillRoundedRectangle (padCard, 18.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (padCard, 18.0f, 1.0f);

    g.setColour (juce::Colour (0xffc6ef73));
    g.fillRoundedRectangle (juce::Rectangle<float> (24.0f, 26.0f, 5.0f, 28.0f), 2.5f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (48);
    modeButton.setBounds (header.removeFromRight (112).reduced (0, 5));
    header.removeFromRight (8);
    playButton.setBounds (header.removeFromRight (96).reduced (0, 5));
    header.removeFromRight (8);
    nightcoreButton.setBounds (header.removeFromRight (112).reduced (0, 5));
    header.removeFromRight (12);
    title.setBounds (header);

    auto controls = area.removeFromTop (214);
    controls.reduce (12, 12);
    auto fileRow = controls.removeFromTop (32);
    bpmLabel.setBounds (fileRow.removeFromRight (110));
    fileRow.removeFromRight (8);
    fileBox.setBounds (fileRow);

    controls.removeFromTop (12);
    auto effectRow = controls.removeFromTop (32);
    const auto effectW = (effectRow.getWidth() - 16) / 3;
    xEffectBox.setBounds (effectRow.removeFromLeft (effectW));
    effectRow.removeFromLeft (8);
    yEffectBox.setBounds (effectRow.removeFromLeft (effectW));
    effectRow.removeFromLeft (8);
    pressureEffectBox.setBounds (effectRow);

    controls.removeFromTop (12);
    performancePresetBox.setBounds (controls.removeFromTop (32));

    controls.removeFromTop (12);
    auto positionRow = controls.removeFromTop (30);
    positionLabel.setBounds (positionRow.removeFromRight (140));
    positionRow.removeFromRight (8);
    positionSlider.setBounds (positionRow);
    status.setBounds (area.removeFromBottom (34));

    constexpr int meterW = 36;
    auto padArea = area;
    pressureMeter.setBounds (padArea.removeFromRight (meterW + 14).withTrimmedLeft (10));
    const auto side = juce::jmin (padArea.getWidth(), padArea.getHeight() - 18);
    pad.setBounds (padArea.withSizeKeepingCentre (side, side));
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (! useExternalInput.load())
            togglePlayback();

        return true;
    }

    return false;
}

void MainComponent::setupComboBoxes()
{
    fileBox.setTextWhenNothingSelected ("Select music file");
    fileBox.onChange = [this] { loadSelectedAudioFile(); };

    auto addEffects = [] (juce::ComboBox& box)
    {
        box.addItem (effectName (EffectType::off), static_cast<int> (EffectType::off));
        box.addItem (effectName (EffectType::gate), static_cast<int> (EffectType::gate));
        box.addItem (effectName (EffectType::echo), static_cast<int> (EffectType::echo));
        box.addItem (effectName (EffectType::reverb), static_cast<int> (EffectType::reverb));
        box.addItem (effectName (EffectType::filter), static_cast<int> (EffectType::filter));
        box.addItem (effectName (EffectType::lowPass), static_cast<int> (EffectType::lowPass));
        box.addItem (effectName (EffectType::highPass), static_cast<int> (EffectType::highPass));
        box.addItem (effectName (EffectType::bandPass), static_cast<int> (EffectType::bandPass));
        box.addItem (effectName (EffectType::notch), static_cast<int> (EffectType::notch));
        box.addItem (effectName (EffectType::peakEq), static_cast<int> (EffectType::peakEq));
        box.addItem (effectName (EffectType::lowShelf), static_cast<int> (EffectType::lowShelf));
        box.addItem (effectName (EffectType::highShelf), static_cast<int> (EffectType::highShelf));
        box.addItem (effectName (EffectType::ladder), static_cast<int> (EffectType::ladder));
        box.addItem (effectName (EffectType::flanger), static_cast<int> (EffectType::flanger));
        box.addItem (effectName (EffectType::phaser), static_cast<int> (EffectType::phaser));
        box.addItem (effectName (EffectType::chorus), static_cast<int> (EffectType::chorus));
        box.addItem (effectName (EffectType::tremolo), static_cast<int> (EffectType::tremolo));
        box.addItem (effectName (EffectType::autoPan), static_cast<int> (EffectType::autoPan));
        box.addItem (effectName (EffectType::drive), static_cast<int> (EffectType::drive));
        box.addItem (effectName (EffectType::bitCrusher), static_cast<int> (EffectType::bitCrusher));
        box.addItem (effectName (EffectType::compressor), static_cast<int> (EffectType::compressor));
        box.addItem (effectName (EffectType::roll), static_cast<int> (EffectType::roll));
    };

    xEffectBox.setTextWhenNothingSelected ("X Effect");
    yEffectBox.setTextWhenNothingSelected ("Y Effect");
    pressureEffectBox.setTextWhenNothingSelected ("Pressure Effect");
    performancePresetBox.setTextWhenNothingSelected ("Performance preset");
    addEffects (xEffectBox);
    addEffects (yEffectBox);
    addEffects (pressureEffectBox);
    xEffectBox.setSelectedId (static_cast<int> (EffectType::peakEq), juce::dontSendNotification);
    yEffectBox.setSelectedId (static_cast<int> (EffectType::off), juce::dontSendNotification);
    pressureEffectBox.setSelectedId (static_cast<int> (EffectType::off), juce::dontSendNotification);

    // ComboBox上ではASCIIだけを使う。環境依存フォントで長いダッシュ等が
    // 文字化けしないようにし、現場で確実に読める名前にする。
    performancePresetBox.addItem ("Custom", 1);
    performancePresetBox.addItem ("Ink Accent", 2);
    performancePresetBox.addItem ("Ink Bleed", 3);
    performancePresetBox.addItem ("Rhythm Strokes", 4);
    performancePresetBox.addItem ("Sweep Calligraphy", 5);
    performancePresetBox.addItem ("Soft Wash", 6);
    performancePresetBox.addItem ("Echo Script", 7);
    performancePresetBox.addItem ("Glitch Dots", 8);
    performancePresetBox.addItem ("Bass Stroke", 9);
    performancePresetBox.addItem ("Bright Stroke", 10);
    performancePresetBox.addItem ("Air Brush", 11);
    performancePresetBox.addSeparator();
    performancePresetBox.addItem ("SDVX Laser Wobble", 12);
    performancePresetBox.addItem ("SDVX FX Gate", 13);
    performancePresetBox.addItem ("SDVX Retrigger", 14);
    performancePresetBox.addItem ("SDVX Jet Flanger", 15);
    performancePresetBox.addItem ("SDVX Noisy Filter", 16);
    performancePresetBox.addItem ("SDVX Live Reverb", 17);
    performancePresetBox.setSelectedId (2, juce::dontSendNotification);

    xEffectBox.onChange = [this]
    {
        xEffect = static_cast<EffectType> (xEffectBox.getSelectedId());
        audioXEffect.store (static_cast<int> (xEffect));
        updatePadLabels();
        markCustomPreset();
        shouldResetMomentaryState.store (true);
    };

    yEffectBox.onChange = [this]
    {
        yEffect = static_cast<EffectType> (yEffectBox.getSelectedId());
        audioYEffect.store (static_cast<int> (yEffect));
        updatePadLabels();
        markCustomPreset();
        shouldResetMomentaryState.store (true);
    };

    pressureEffectBox.onChange = [this]
    {
        pressureEffect = static_cast<EffectType> (pressureEffectBox.getSelectedId());
        audioPressureEffect.store (static_cast<int> (pressureEffect));
        updatePadLabels();
        markCustomPreset();
        shouldResetMomentaryState.store (true);
    };

    performancePresetBox.onChange = [this]
    {
        if (! applyingPreset)
            applyPerformancePreset (performancePresetBox.getSelectedId());
    };

    applyPerformancePreset (2);
    updatePadLabels();
}

void MainComponent::applyPerformancePreset (int presetId)
{
    if (presetId == 1)
        return;

    struct Preset
    {
        EffectType x;
        EffectType y;
        EffectType pressure;
    };

    Preset preset { EffectType::peakEq, EffectType::off, EffectType::off };

    switch (presetId)
    {
        case 2: // 音程帯を筆圧で強調。文字の輪郭を邪魔しない基準プリセット。
            preset = { EffectType::peakEq, EffectType::off, EffectType::off };
            break;
        case 3: // 払いに合わせてフィルターを開き、荷重で残響を深くする。
            preset = { EffectType::ladder, EffectType::off, EffectType::reverb };
            break;
        case 4: // 筆運びでDJフィルター、強い筆圧で拍に沿ったゲートを加える。
            preset = { EffectType::filter, EffectType::off, EffectType::gate };
            break;
        case 5: // 横方向でフィルター、縦方向でピーク帯域をなぞる。
            preset = { EffectType::filter, EffectType::peakEq, EffectType::off };
            break;
        case 6: // 薄い筆致では原音、押すほどローパスと残響を重ねる。
            preset = { EffectType::lowPass, EffectType::off, EffectType::reverb };
            break;
        case 7: // 線の位置で強調帯域、筆圧でテンポ同期Echoを深くする。
            preset = { EffectType::peakEq, EffectType::off, EffectType::echo };
            break;
        case 8: // 点・跳ねにだけロールを入れる、リズムを強調した表現。
            preset = { EffectType::bitCrusher, EffectType::off, EffectType::roll };
            break;
        case 9: // 太い横画を低域の押し出しとラダーのうねりへ結び付ける。
            preset = { EffectType::lowShelf, EffectType::ladder, EffectType::compressor };
            break;
        case 10: // 縦の払いを高域、強い止めをGateで光らせる。
            preset = { EffectType::highShelf, EffectType::phaser, EffectType::gate };
            break;
        case 11: // 大きな筆運び向けの広がり。空間系を重ねる実験用。
            preset = { EffectType::chorus, EffectType::autoPan, EffectType::reverb };
            break;
        // 以下はSDVXの操作感を既存DSPで再構成したプリセット。
        // 厳密なゲーム内DSPの複製ではなく、左右のつまみ=X/Y、FXボタン=筆圧という対応。
        case 12:
            preset = { EffectType::filter, EffectType::ladder, EffectType::off };
            break;
        case 13:
            preset = { EffectType::filter, EffectType::off, EffectType::gate };
            break;
        case 14:
            preset = { EffectType::filter, EffectType::off, EffectType::roll };
            break;
        case 15:
            preset = { EffectType::flanger, EffectType::phaser, EffectType::off };
            break;
        case 16:
            preset = { EffectType::bandPass, EffectType::highPass, EffectType::bitCrusher };
            break;
        case 17:
            preset = { EffectType::filter, EffectType::off, EffectType::reverb };
            break;
        default:
            return;
    }

    applyingPreset = true;
    xEffect = preset.x;
    yEffect = preset.y;
    pressureEffect = preset.pressure;
    xEffectBox.setSelectedId (static_cast<int> (preset.x), juce::dontSendNotification);
    yEffectBox.setSelectedId (static_cast<int> (preset.y), juce::dontSendNotification);
    pressureEffectBox.setSelectedId (static_cast<int> (preset.pressure), juce::dontSendNotification);
    performancePresetBox.setSelectedId (presetId, juce::dontSendNotification);
    audioXEffect.store (static_cast<int> (preset.x));
    audioYEffect.store (static_cast<int> (preset.y));
    audioPressureEffect.store (static_cast<int> (preset.pressure));
    applyingPreset = false;
    shouldResetMomentaryState.store (true);
    updatePadLabels();
}

void MainComponent::markCustomPreset()
{
    if (! applyingPreset)
        performancePresetBox.setSelectedId (1, juce::dontSendNotification);
}

void MainComponent::refreshAudioFiles()
{
    audioFiles.clear();
    fileBox.clear (juce::dontSendNotification);

    for (const auto& directory : getAudioDirectories())
    {
        if (! directory.isDirectory())
            continue;

        for (const auto& pattern : { "*.m4a", "*.mp3", "*.wav", "*.aiff" })
        {
            juce::Array<juce::File> matches;
            directory.findChildFiles (matches, juce::File::findFiles, false, pattern);

            for (const auto& file : matches)
                if (! audioFiles.contains (file))
                    audioFiles.add (file);
        }
    }

    audioFiles.sort();

    for (int i = 0; i < audioFiles.size(); ++i)
        fileBox.addItem (audioFiles[i].getFileName(), i + 1);

    if (! audioFiles.isEmpty())
        fileBox.setSelectedId (1, juce::dontSendNotification);
}

void MainComponent::loadSelectedAudioFile()
{
    systemCaptureStatus.clear();
    const auto index = fileBox.getSelectedId() - 1;

    if (! juce::isPositiveAndBelow (index, audioFiles.size()))
    {
        updateStatus();
        return;
    }

    const auto audioFile = audioFiles[index];
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));

    if (reader == nullptr)
    {
        status.setText ("Could not read: " + audioFile.getFileName(), juce::dontSendNotification);
        return;
    }

    tempoAnalysis = TempoAnalyzer::analyse (audioFile, formatManager);
    currentBpm.store (tempoAnalysis.globalBpm);
    pendingBpm = tempoAnalysis.globalBpm;
    pendingBpmTicks = 0;
    updateBpmLabel();

    const auto wasPlaying = shouldBePlaying.load();
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();

    loadedSourceSampleRate = reader->sampleRate;
    readerSource.reset (new juce::AudioFormatReaderSource (reader.release(), true));
    readerSource->setLooping (true);
    reconnectTransportSource (0.0);
    shouldBePlaying.store (wasPlaying);

    if (wasPlaying && ! useExternalInput.load())
        transport.start();

    positionSlider.setRange (0.0, juce::jmax (0.001, transport.getLengthInSeconds()), 0.001);
    positionSlider.setValue (0.0, juce::dontSendNotification);
    loadedFileName = audioFile.getFileName();
    shouldResetMomentaryState.store (true);
    updateStatus();
    updatePositionDisplay();
    updatePlayButton();
    updateNightcoreButton();
}

juce::Array<juce::File> MainComponent::getAudioDirectories() const
{
    juce::Array<juce::File> directories;
    directories.add (juce::File (SOURCE_AUDIO_DIR));
    directories.add (juce::File::getCurrentWorkingDirectory());
    directories.add (juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                         .getParentDirectory().getChildFile ("Resources"));
    return directories;
}

void MainComponent::timerCallback()
{
    updateControllerPadDisplay();

    if (! useExternalInput.load() && readerSource != nullptr && shouldBePlaying.load() && ! transport.isPlaying())
        transport.start();

    if (! useExternalInput.load() && readerSource != nullptr)
    {
        const auto localBpm = tempoAnalysis.bpmAtTime (transport.getCurrentPosition());
        const auto displayedBpm = currentBpm.load();

        if (std::abs (localBpm - displayedBpm) >= 6.0f)
        {
            if (std::abs (localBpm - pendingBpm) < 3.0f)
                ++pendingBpmTicks;
            else
            {
                pendingBpm = localBpm;
                pendingBpmTicks = 1;
            }

            if (pendingBpmTicks >= 20)
                currentBpm.store (pendingBpm);
        }
        else
        {
            pendingBpm = displayedBpm;
            pendingBpmTicks = 0;
        }

        updateBpmLabel();
        updatePositionDisplay();
    }
}

void MainComponent::updateModeButton()
{
    modeButton.setButtonText (useExternalInput.load() ? "PC Audio" : "Track");

    if (useExternalInput.load())
        transport.stop();
    else if (readerSource != nullptr && shouldBePlaying.load())
        transport.start();

    updatePositionDisplay();
    updatePlayButton();
    updateNightcoreButton();
}

void MainComponent::updatePadLabels()
{
    pad.setAxisLabels ("X: " + effectName (xEffect)
                           + "  |  P: " + effectName (pressureEffect),
                       "Y: " + effectName (yEffect));
}

void MainComponent::updateBpmLabel()
{
    bpmLabel.setText ("BPM " + juce::String (getMusicalBpm(), 1), juce::dontSendNotification);
}

void MainComponent::updatePositionDisplay()
{
    if (useExternalInput.load() || readerSource == nullptr)
    {
        positionLabel.setText ("--:-- / --:--", juce::dontSendNotification);
        positionSlider.setEnabled (false);
        return;
    }

    positionSlider.setEnabled (true);
    const auto position = transport.getCurrentPosition();
    const auto length = transport.getLengthInSeconds();

    if (! userIsSeeking.load())
        positionSlider.setValue (position, juce::dontSendNotification);

    positionLabel.setText (formatTime (position) + " / " + formatTime (length), juce::dontSendNotification);
}

void MainComponent::updatePlayButton()
{
    playButton.setButtonText (shouldBePlaying.load() ? "Pause" : "Play");
    playButton.setEnabled (! useExternalInput.load() && readerSource != nullptr);
}

void MainComponent::updateNightcoreButton()
{
    nightcoreButton.setButtonText (nightcoreEnabled.load() ? "Night ON" : "Nightcore");
    nightcoreButton.setEnabled (! useExternalInput.load() && readerSource != nullptr);
}

void MainComponent::updateControllerPadDisplay()
{
    pressureMeter.setLevel (controllerGuiPressure.load());

    if (! controllerGuiDirty.exchange (false))
        return;

    pad.setExternalPosition (controllerGuiX.load(),
                             controllerGuiY.load(),
                             controllerGuiTouching.load(),
                             false);
}

void MainComponent::updateStatus()
{
    const auto suffix = controllerStatus.isNotEmpty() ? (" | " + controllerStatus) : "";

    if (systemCaptureStatus.isNotEmpty())
    {
        status.setText (systemCaptureStatus + suffix, juce::dontSendNotification);
        return;
    }

    if (useExternalInput.load())
    {
        status.setText ("Capturing PC audio. Hold pad for FX, release for dry." + suffix,
                        juce::dontSendNotification);
        return;
    }

    if (loadedFileName.isNotEmpty())
        status.setText ("Playing: " + loadedFileName + suffix, juce::dontSendNotification);
    else
        status.setText ("No audio file found next to the app or in the source folder." + suffix, juce::dontSendNotification);
}

void MainComponent::handleControllerSample (const ControllerSample& sample)
{
    // タッチ閾値〜しっかり押した荷重を 0..1 の筆圧に正規化
    constexpr float pressureMinGrams = 5.0f;
    constexpr float pressureMaxGrams = 90.0f;
    const auto normalisedPressure = juce::jlimit (
        0.0f, 1.0f,
        (sample.totalGrams - pressureMinGrams) / (pressureMaxGrams - pressureMinGrams));

    audioPadX.store (sample.x);
    audioPadY.store (sample.y);
    audioPadPressure.store (sample.touching ? normalisedPressure : 0.0f);
    audioEffectsActive.store (sample.touching);

    if (lastPointerDown.exchange (sample.touching) != sample.touching)
        shouldResetMomentaryState.store (true);

    controllerGuiX.store (sample.x);
    controllerGuiY.store (sample.y);
    controllerGuiPressure.store (sample.touching ? normalisedPressure : 0.0f);
    controllerGuiTouching.store (sample.touching);
    controllerGuiDirty.store (true);
}

void MainComponent::setControllerStatus (juce::String message)
{
    controllerStatus = std::move (message);
    updateStatus();
}

void MainComponent::togglePlayback()
{
    setPlaybackWanted (! shouldBePlaying.load());
}

void MainComponent::toggleNightcore()
{
    const auto sourcePosition = transport.getCurrentPosition() * getPlaybackRateMultiplier();
    nightcoreEnabled.store (! nightcoreEnabled.load());

    if (! useExternalInput.load() && readerSource != nullptr)
        reconnectTransportSource (sourcePosition);

    updateNightcoreButton();
    updateBpmLabel();
    updatePositionDisplay();
}

void MainComponent::reconnectTransportSource (double sourcePositionToRestore)
{
    if (readerSource == nullptr || loadedSourceSampleRate <= 0.0)
        return;

    const auto wasPlaying = shouldBePlaying.load() && transport.isPlaying();
    transport.stop();
    transport.setSource (nullptr);
    transport.setSource (readerSource.get(), 0, nullptr, loadedSourceSampleRate * getPlaybackRateMultiplier());
    transport.setPosition (juce::jmax (0.0, sourcePositionToRestore / getPlaybackRateMultiplier()));
    positionSlider.setRange (0.0, juce::jmax (0.001, transport.getLengthInSeconds()), 0.001);

    if (wasPlaying || (shouldBePlaying.load() && ! useExternalInput.load()))
        transport.start();
}

void MainComponent::setPlaybackWanted (bool shouldPlay)
{
    shouldBePlaying.store (shouldPlay);

    if (useExternalInput.load() || readerSource == nullptr)
    {
        updatePlayButton();
        return;
    }

    if (shouldPlay)
        transport.start();
    else
        transport.stop();

    updatePlayButton();
    updatePositionDisplay();
    grabKeyboardFocus();
}

double MainComponent::getPlaybackRateMultiplier() const
{
    return nightcoreEnabled.load() ? 1.18 : 1.0;
}

float MainComponent::getMusicalBpm() const
{
    return juce::jlimit (70.0f, 230.0f, currentBpm.load() * static_cast<float> (getPlaybackRateMultiplier()));
}

juce::String MainComponent::findBlackHoleInputName()
{
    const auto& types = deviceManager.getAvailableDeviceTypes();

    for (auto* type : types)
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();

        for (const auto& name : type->getDeviceNames (true))
            if (name.containsIgnoreCase ("BlackHole"))
                return name;
    }

    return {};
}

juce::String MainComponent::findPlaybackOutputName()
{
    const auto preferred = SystemAudioRouter::findPreferredPlaybackOutputName();

    if (preferred.isNotEmpty() && ! preferred.containsIgnoreCase ("BlackHole"))
        return preferred;

    juce::StringArray candidates;
    const auto& types = deviceManager.getAvailableDeviceTypes();

    for (auto* type : types)
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();
        candidates = type->getDeviceNames (false);

        if (! candidates.isEmpty())
            break;
    }

    for (const auto& name : candidates)
        if (! name.containsIgnoreCase ("BlackHole"))
            return name;

    return {};
}

void MainComponent::applyTrackModeRouting()
{
    juce::String routeError;
    SystemAudioRouter::ensureSystemOutputAvoidsBlackHole (routeError);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);

    setup.inputDeviceName.clear();
    setup.inputChannels.clear();
    setup.useDefaultInputChannels = false;

    const auto playbackOutput = findPlaybackOutputName();

    if (playbackOutput.isNotEmpty())
        setup.outputDeviceName = playbackOutput;

    setup.useDefaultOutputChannels = true;
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true);

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (routeError.isNotEmpty())
        systemCaptureStatus = routeError;
    else if (error.isNotEmpty())
        systemCaptureStatus = "Failed to open playback device: " + error;
    else
        systemCaptureStatus.clear();
}

bool MainComponent::applySystemCaptureRouting()
{
    const auto blackHole = findBlackHoleInputName();

    if (blackHole.isEmpty())
    {
        systemCaptureStatus = "BlackHole not found. Run Tools/install_blackhole.sh first.";
        return false;
    }

    const auto playbackOutput = findPlaybackOutputName();

    if (playbackOutput.isEmpty())
    {
        systemCaptureStatus = "No speaker/headphone output device found.";
        return false;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    setup.inputDeviceName = blackHole;
    setup.outputDeviceName = playbackOutput;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true);
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true);

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
    {
        systemCaptureStatus = "Failed to open BlackHole: " + error;
        return false;
    }

    juce::String routeError;

    if (! SystemAudioRouter::routeSystemOutputToBlackHole (routeError))
    {
        restoreDefaultRouting();
        systemCaptureStatus = routeError;
        return false;
    }

    systemCaptureStatus = "PC Audio: system -> BlackHole -> this app -> " + playbackOutput
                          + ". Hold pad for FX.";
    return true;
}

void MainComponent::restoreDefaultRouting()
{
    juce::String routeError;
    SystemAudioRouter::restoreSystemOutput (routeError);

    if (routeError.isNotEmpty())
        SystemAudioRouter::ensureSystemOutputAvoidsBlackHole (routeError);

    applyTrackModeRouting();

    if (routeError.isNotEmpty() && systemCaptureStatus.isEmpty())
        systemCaptureStatus = routeError;
}

juce::String MainComponent::formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const auto totalSeconds = static_cast<int> (std::round (seconds));
    const auto minutes = totalSeconds / 60;
    const auto remainingSeconds = totalSeconds % 60;
    return juce::String (minutes) + ":" + juce::String (remainingSeconds).paddedLeft ('0', 2);
}
