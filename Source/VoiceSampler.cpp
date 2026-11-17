// Voice Sampler - load any voice/audio file and play it from MIDI in FL Studio.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

using namespace juce;

//==============================================================================
class VoiceSamplerProcessor : public AudioProcessor,
                              public AudioProcessorValueTreeState::Listener,
                              private AsyncUpdater
{
public:
    VoiceSamplerProcessor()
        : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", createLayout())
    {
        formatManager.registerBasicFormats();
        for (int i = 0; i < 16; ++i)
            synth.addVoice(new SamplerVoice());

        for (auto* id : { "attack", "release", "root" })
            apvts.addParameterListener(id, this);
    }

    ~VoiceSamplerProcessor() override
    {
        cancelPendingUpdate();
        for (auto* id : { "attack", "release", "root" })
            apvts.removeParameterListener(id, this);
    }

    static AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        std::vector<std::unique_ptr<RangedAudioParameter>> p;
        p.push_back(std::make_unique<AudioParameterFloat>("gain", "Gain", NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
        p.push_back(std::make_unique<AudioParameterFloat>("attack", "Attack", NormalisableRange<float>(0.0f, 2.0f, 0.001f, 0.5f), 0.005f));
        p.push_back(std::make_unique<AudioParameterFloat>("release", "Release", NormalisableRange<float>(0.0f, 5.0f, 0.001f, 0.5f), 0.2f));
        p.push_back(std::make_unique<AudioParameterInt>("root", "Root Note", 0, 127, 60));
        return { p.begin(), p.end() };
    }

    //==========================================================================
    bool loadSample(const File& f)
    {
        std::unique_ptr<AudioFormatReader> r(formatManager.createReaderFor(f));
        if (r == nullptr)
            return false;
        sampleFile = f;
        rebuildSound();
        return true;
    }

    void rebuildSound()
    {
        if (!sampleFile.existsAsFile())
            return;
        std::unique_ptr<AudioFormatReader> reader(formatManager.createReaderFor(sampleFile));
        if (reader == nullptr)
            return;

        BigInteger range;
        range.setRange(0, 128, true);

        auto* sound = new SamplerSound(sampleFile.getFileNameWithoutExtension(), *reader, range,
                                       (int) *apvts.getRawParameterValue("root"),
                                       (double) *apvts.getRawParameterValue("attack"),
                                       (double) *apvts.getRawParameterValue("release"),
                                       30.0); // max 30 seconds of audio

        const ScopedLock sl(lock);
        synth.clearSounds();
        synth.addSound(sound);
    }

    void parameterChanged(const String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuildSound(); }

    //==========================================================================
    void prepareToPlay(double sr, int) override { synth.setCurrentPlaybackSampleRate(sr); }
    void releaseResources() override {}

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi) override
    {
        ScopedNoDenormals noDenormals;
        buffer.clear();
        keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);

        {
            const ScopedTryLock sl(lock);
            if (sl.isLocked())
                synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
        }
        buffer.applyGain(*apvts.getRawParameterValue("gain"));
    }

    //==========================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const String getName() const override { return "Voice Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}

    void getStateInformation(MemoryBlock& dest) override
    {
        auto state = apvts.copyState();
        state.setProperty("file", sampleFile.getFullPathName(), nullptr);
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, dest);
    }

    void setStateInformation(const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary(data, size))
        {
            auto tree = ValueTree::fromXml(*xml);
            if (tree.isValid())
            {
                apvts.replaceState(tree);
                sampleFile = File(tree["file"].toString());
                rebuildSound();
            }
        }
    }

    AudioProcessorValueTreeState apvts;
    MidiKeyboardState keyboardState;
    File sampleFile;

private:
    Synthesiser synth;
    AudioFormatManager formatManager;
    CriticalSection lock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceSamplerProcessor)
};

//==============================================================================
class VoiceSamplerEditor : public AudioProcessorEditor,
                           public FileDragAndDropTarget
{
public:
    explicit VoiceSamplerEditor(VoiceSamplerProcessor& p)
        : AudioProcessorEditor(&p), proc(p),
          keyboard(p.keyboardState, MidiKeyboardComponent::horizontalKeyboard)
    {
        addAndMakeVisible(loadButton);
        loadButton.setButtonText("Load voice...");
        loadButton.onClick = [this] { chooseFile(); };

        addAndMakeVisible(fileLabel);
        fileLabel.setJustificationType(Justification::centred);
        updateLabel();

        setupSlider(gainSlider, gainLabel, "Gain", "gain", gainAtt);
        setupSlider(attackSlider, attackLabel, "Attack", "attack", attackAtt);
        setupSlider(releaseSlider, releaseLabel, "Release", "release", releaseAtt);
        setupSlider(rootSlider, rootLabel, "Root", "root", rootAtt);
        rootSlider.setNumDecimalPlacesToDisplay(0);

        addAndMakeVisible(keyboard);
        keyboard.setKeyWidth(18.0f);

        setSize(620, 330);
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xff1d1f24));
        g.setColour(Colours::white);
        g.setFont(FontOptions(22.0f, Font::bold));
        g.drawText("VOICE SAMPLER", 0, 8, getWidth(), 30, Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        r.removeFromTop(34);
        loadButton.setBounds(r.removeFromTop(32));
        fileLabel.setBounds(r.removeFromTop(28));

        auto knobs = r.removeFromTop(120);
        int w = knobs.getWidth() / 4;
        placeKnob(knobs.removeFromLeft(w), gainSlider, gainLabel);
        placeKnob(knobs.removeFromLeft(w), attackSlider, attackLabel);
        placeKnob(knobs.removeFromLeft(w), releaseSlider, releaseLabel);
        placeKnob(knobs, rootSlider, rootLabel);

        keyboard.setBounds(r.removeFromBottom(80));
    }

    bool isInterestedInFileDrag(const StringArray&) override { return true; }

    void filesDropped(const StringArray& files, int, int) override
    {
        if (files.size() > 0 && proc.loadSample(File(files[0])))
            updateLabel();
    }

private:
    void chooseFile()
    {
        chooser = std::make_unique<FileChooser>("Select a voice file", File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                             [this](const FileChooser& fc)
                             {
                                 auto f = fc.getResult();
                                 if (f.existsAsFile() && proc.loadSample(f))
                                     updateLabel();
                             });
    }

    void updateLabel()
    {
        fileLabel.setText(proc.sampleFile.existsAsFile() ? proc.sampleFile.getFileName()
                                                         : "Drag & drop a voice file here (or click Load)",
                          dontSendNotification);
    }

    void setupSlider(Slider& s, Label& l, const String& name, const String& id,
                     std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>& att)
    {
        s.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(Slider::TextBoxBelow, false, 70, 18);
        addAndMakeVisible(s);
        l.setText(name, dontSendNotification);
        l.setJustificationType(Justification::centred);
        addAndMakeVisible(l);
        att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, s);
    }

    static void placeKnob(Rectangle<int> area, Slider& s, Label& l)
    {
        l.setBounds(area.removeFromTop(20));
        s.setBounds(area);
    }

    VoiceSamplerProcessor& proc;
    TextButton loadButton;
    Label fileLabel, gainLabel, attackLabel, releaseLabel, rootLabel;
    Slider gainSlider, attackSlider, releaseSlider, rootSlider;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> gainAtt, attackAtt, releaseAtt, rootAtt;
    MidiKeyboardComponent keyboard;
    std::unique_ptr<FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceSamplerEditor)
};

AudioProcessorEditor* VoiceSamplerProcessor::createEditor() { return new VoiceSamplerEditor(*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoiceSamplerProcessor(); }
