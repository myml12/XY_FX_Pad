#include "MainComponent.h"

MainComponent::MainComponent()
{
    formatManager.registerBasicFormats();

   #if JUCE_MAC
    formatManager.registerFormat (new juce::CoreAudioFormat(), true);
   #endif

    pad.onPadChanged = [this] (float x, float y, bool down)
    {
        juce::ScopedLock lock (stateLock);
        padX = x;
        padY = y;
        effectsActive = down;

        if (down != lastPointerDown)
        {
            lastPointerDown = down;
            shouldResetMomentaryState.store (true);
        }
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
    addAndMakeVisible (title);
    addAndMakeVisible (modeButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (nightcoreButton);
    addAndMakeVisible (fileBox);
    addAndMakeVisible (xEffectBox);
    addAndMakeVisible (yEffectBox);
    addAndMakeVisible (positionSlider);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (status);

    title.setText ("DJ XY Pad", juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));

    modeButton.setButtonText ("File");
    modeButton.onClick = [this]
    {
        useExternalInput.store (! useExternalInput.load());
        updateModeButton();
        updateStatus();
    };

    playButton.setButtonText ("Pause");
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

    loadSelectedAudioFile();
    setSize (600, 700);
    setAudioChannels (2, 2);
    startTimerHz (20);
    updatePlayButton();
    updateNightcoreButton();
    controllerReceiver.start();
}

MainComponent::~MainComponent()
{
    stopTimer();
    controllerReceiver.stop();
    pad.removeKeyListener (this);
    positionSlider.removeKeyListener (this);
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
    shutdownAudio();
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    transport.prepareToPlay (samplesPerBlockExpected, sampleRate);
    xEffectProcessor.prepare (samplesPerBlockExpected, sampleRate);
    yEffectProcessor.prepare (samplesPerBlockExpected, sampleRate);

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

    bool active;
    float x;
    float y;
    EffectType selectedXEffect;
    EffectType selectedYEffect;
    {
        juce::ScopedLock lock (stateLock);
        active = effectsActive;
        x = padX;
        y = padY;
        selectedXEffect = xEffect;
        selectedYEffect = yEffect;
    }

    if (! active)
        return;

    if (shouldResetMomentaryState.exchange (false))
    {
        xEffectProcessor.resetMomentary();
        yEffectProcessor.resetMomentary();
    }

    const auto bpm = getMusicalBpm();
    xEffectProcessor.process (bufferToFill, selectedXEffect, x, bpm);
    yEffectProcessor.process (bufferToFill, selectedYEffect, y, bpm);
}

void MainComponent::releaseResources()
{
    transport.stop();
    transport.releaseResources();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff151820));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (44);
    modeButton.setBounds (header.removeFromRight (96).reduced (0, 4));
    playButton.setBounds (header.removeFromRight (96).reduced (0, 4));
    nightcoreButton.setBounds (header.removeFromRight (112).reduced (0, 4));
    title.setBounds (header);

    auto controls = area.removeFromTop (130);
    auto fileRow = controls.removeFromTop (28);
    bpmLabel.setBounds (fileRow.removeFromRight (110));
    fileRow.removeFromRight (8);
    fileBox.setBounds (fileRow);

    controls.removeFromTop (10);
    auto effectRow = controls.removeFromTop (28);
    auto xArea = effectRow.removeFromLeft (effectRow.getWidth() / 2);
    xArea.removeFromRight (6);
    effectRow.removeFromLeft (6);
    xEffectBox.setBounds (xArea);
    yEffectBox.setBounds (effectRow);

    controls.removeFromTop (10);
    auto positionRow = controls.removeFromTop (28);
    positionLabel.setBounds (positionRow.removeFromRight (140));
    positionRow.removeFromRight (8);
    positionSlider.setBounds (positionRow);
    status.setBounds (area.removeFromBottom (38));

    const auto side = juce::jmin (area.getWidth(), area.getHeight() - 20);
    pad.setBounds (area.withSizeKeepingCentre (side, side));
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::spaceKey)
    {
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
        box.addItem (effectName (EffectType::gate), static_cast<int> (EffectType::gate));
        box.addItem (effectName (EffectType::echo), static_cast<int> (EffectType::echo));
        box.addItem (effectName (EffectType::reverb), static_cast<int> (EffectType::reverb));
        box.addItem (effectName (EffectType::filter), static_cast<int> (EffectType::filter));
        box.addItem (effectName (EffectType::lowPass), static_cast<int> (EffectType::lowPass));
        box.addItem (effectName (EffectType::highPass), static_cast<int> (EffectType::highPass));
        box.addItem (effectName (EffectType::bandPass), static_cast<int> (EffectType::bandPass));
        box.addItem (effectName (EffectType::notch), static_cast<int> (EffectType::notch));
        box.addItem (effectName (EffectType::flanger), static_cast<int> (EffectType::flanger));
        box.addItem (effectName (EffectType::phaser), static_cast<int> (EffectType::phaser));
        box.addItem (effectName (EffectType::tremolo), static_cast<int> (EffectType::tremolo));
        box.addItem (effectName (EffectType::drive), static_cast<int> (EffectType::drive));
        box.addItem (effectName (EffectType::bitCrusher), static_cast<int> (EffectType::bitCrusher));
        box.addItem (effectName (EffectType::roll), static_cast<int> (EffectType::roll));
    };

    xEffectBox.setTextWhenNothingSelected ("X Effect");
    yEffectBox.setTextWhenNothingSelected ("Y Effect");
    addEffects (xEffectBox);
    addEffects (yEffectBox);
    xEffectBox.setSelectedId (static_cast<int> (EffectType::filter), juce::dontSendNotification);
    yEffectBox.setSelectedId (static_cast<int> (EffectType::echo), juce::dontSendNotification);

    xEffectBox.onChange = [this]
    {
        juce::ScopedLock lock (stateLock);
        xEffect = static_cast<EffectType> (xEffectBox.getSelectedId());
        updatePadLabels();
        shouldResetMomentaryState.store (true);
    };

    yEffectBox.onChange = [this]
    {
        juce::ScopedLock lock (stateLock);
        yEffect = static_cast<EffectType> (yEffectBox.getSelectedId());
        updatePadLabels();
        shouldResetMomentaryState.store (true);
    };

    updatePadLabels();
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
    modeButton.setButtonText (useExternalInput.load() ? "Input" : "File");

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
    pad.setAxisLabels ("X: " + effectName (xEffect), "Y: " + effectName (yEffect));
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

    if (useExternalInput.load())
    {
        status.setText ("External input -> output. Hold the pad to apply effects." + suffix, juce::dontSendNotification);
        return;
    }

    if (loadedFileName.isNotEmpty())
        status.setText ("Playing: " + loadedFileName + suffix, juce::dontSendNotification);
    else
        status.setText ("No audio file found next to the app or in the source folder." + suffix, juce::dontSendNotification);
}

void MainComponent::handleControllerSample (const ControllerSample& sample)
{
    bool resetMomentary = false;
    {
        juce::ScopedLock lock (stateLock);
        padX = sample.x;
        padY = sample.y;
        effectsActive = sample.touching;

        if (sample.touching != lastPointerDown)
        {
            lastPointerDown = sample.touching;
            resetMomentary = true;
        }
    }

    if (resetMomentary)
        shouldResetMomentaryState.store (true);

    controllerGuiX.store (sample.x);
    controllerGuiY.store (sample.y);
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

juce::String MainComponent::formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const auto totalSeconds = static_cast<int> (std::round (seconds));
    const auto minutes = totalSeconds / 60;
    const auto remainingSeconds = totalSeconds % 60;
    return juce::String (minutes) + ":" + juce::String (remainingSeconds).paddedLeft ('0', 2);
}
