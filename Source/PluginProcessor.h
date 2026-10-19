#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include "Core/ParameterDefinitions.h"
#include "Core/ClapPolyParameter.h"
#include "Core/LockFreeVisualFifo.h"
#include "Core/FactoryPresets.h"
#include "Core/MidiLearnManager.h"
#include "Core/PresetManager.h"
#include "DSP/SynthEngine.h"
#include <array>
#include <vector>

// Every parameter the audio thread reads; resolved to raw atomics once so processBlock does no string lookups
#define RIC_RAW_PARAMS(X) \
    X(exciterBypass) X(ringsBypass) X(cloudsBypass) \
    X(inputSource) X(oscEngine) X(oscTune) X(oscHarmonics) X(oscTimbre) X(oscMorph) \
    X(subBlend) X(strikeLevel) X(directMix) \
    X(ringsModel) X(ringsPoly) X(ringsStructure) X(ringsBrightness) X(ringsDamping) X(ringsPosition) X(ringsTune) \
    X(cloudsMode) X(cloudsFreeze) X(cloudsPosition) X(cloudsSize) X(cloudsDensity) X(cloudsTexture) \
    X(cloudsPitch) X(cloudsFeedback) X(cloudsReverb) X(cloudsDryWet) X(cloudsSpread) \
    X(lfo1Rate) X(lfo1Shape) X(lfo2Rate) X(lfo2Shape) X(lfo3Rate) X(lfo3Shape) \
    X(lfo1Sync) X(lfo1Division) X(lfo2Sync) X(lfo2Division) X(lfo3Sync) X(lfo3Division) \
    X(env1Attack) X(env1Decay) X(env1Sustain) X(env1Release) \
    X(env2Attack) X(env2Decay) X(env2Sustain) X(env2Release) \
    X(env3Attack) X(env3Decay) X(env3Sustain) X(env3Release) \
    X(marblesEnable) X(marblesTempoSync) X(marblesDivision) X(marblesRate) X(marblesDejaVu) \
    X(marblesSpread) X(marblesSteps) X(marblesLength) X(marblesJitter) X(marblesPitchTarget) \
    X(polyphonyMode) X(masterGain)

class KingsIntoClownsProcessor : public juce::AudioProcessor,
                                 public clap_juce_extensions::clap_properties,
                                 public clap_juce_extensions::clap_juce_audio_processor_capabilities {
public:
    KingsIntoClownsProcessor();
    ~KingsIntoClownsProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // CLAP capabilities overrides
    bool isInputMain(int /*input*/) override { return true; }
    bool supportsVoiceInfo() override { return true; }
    bool voiceInfoGet(clap_voice_info* info) override;
    bool supportsNoteExpressions() override { return true; }
    bool supportsDirectEvent(uint16_t space_id, uint16_t type) override;
    void handleDirectEvent(const clap_event_header_t* event, int sampleOffset) override;
    bool supportsOutboundEvents() override { return true; }
    void addOutboundEventsToQueue(const clap_output_events* out_events,
                                  const juce::MidiBuffer& midiBuffer,
                                  int sampleOffset) override;

    // Accessors for Editor UI
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager_; }
    ric::core::MidiLearnManager& getMidiLearnManager() noexcept { return midiLearn_; }
    ric::core::PresetManager& getPresetManager() noexcept { return *presetManager_; }
    ric::core::LockFreeVisualFifo& getVisualFifo() noexcept { return visualFifo_; }
    ric::dsp::SynthEngine& getSynthEngine() noexcept { return synthEngine_; }
    juce::String getCurrentPoemSubtitle() const;

    // Remembered editor width (the editor keeps a fixed aspect ratio)
    int getSavedEditorWidth() const;
    void setSavedEditorWidth(int width);

    // Audition / Strum trigger from UI
    void triggerManualStrum(int midiNote = 60, float velocity = 0.85f) noexcept;

private:
    enum class Raw : size_t {
#define RIC_RAW_ENUM(name) name,
        RIC_RAW_PARAMS(RIC_RAW_ENUM)
#undef RIC_RAW_ENUM
        Count
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void bindClapPolyCallbacks();
    void cacheRawParameters();
    float raw(Raw r) const noexcept { return rawParams_[static_cast<size_t>(r)]->load(std::memory_order_relaxed); }
    ric::dsp::EngineParameters gatherEngineParameters() const noexcept;

    juce::UndoManager undoManager_;
    juce::AudioProcessorValueTreeState apvts_;
    ric::dsp::SynthEngine synthEngine_;
    ric::core::LockFreeVisualFifo visualFifo_;
    ric::core::MidiLearnManager midiLearn_;
    std::unique_ptr<ric::core::PresetManager> presetManager_;

    std::vector<ric::core::ClapPolyParameter*> polyParams_;
    std::array<std::atomic<float>*, static_cast<size_t>(Raw::Count)> rawParams_{};

    std::atomic<int> pendingAuditionNote_{ -1 };
    std::atomic<float> pendingAuditionVel_{ 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KingsIntoClownsProcessor)
};
