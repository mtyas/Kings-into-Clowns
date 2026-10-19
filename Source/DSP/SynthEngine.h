#pragma once
#include "SynthVoice.h"
#include "Wrappers/CloudsWrapper.h"
#include "Wrappers/MarblesWrapper.h"
#include "../Core/LockFreeVisualFifo.h"
#include "../Core/MidiLearnManager.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace ric::dsp {

struct TerminatedNoteEvent {
    int32_t noteId = -1;
    int16_t portIndex = 0;
    int16_t channel = 0;
    int16_t key = 60;
    uint32_t sampleOffset = 0;
};

struct EngineParameters {
    VoiceContextParams voiceCtx{};
    CloudsPatch clouds{};
    MarblesPatch marbles{};
    int maxPolyphony = 8; // 1, 4, 8, or 16
    double hostBpm = 120.0;
    double hostPpqAtBlockStart = 0.0; // quarter notes since song start
    bool hostPlaying = false;
    float masterGainLinear = 1.0f;
};

class SynthEngine {
public:
    static constexpr size_t kMaxVoices = 16;
    static constexpr size_t kSubBlockSize = 24;

    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    SynthVoice* handleNoteOn(int midiNote, float velocity, int16_t channel, int32_t clapNoteId = -1) noexcept;
    void handleNoteOff(int midiNote, float velocity, int16_t channel, int32_t clapNoteId = -1) noexcept;
    void handleAllNotesOff() noexcept;

    // MPE & CLAP per-note modulation hooks
    void handleChannelPressure(int16_t channel, float pressure) noexcept;
    void handleController(int16_t channel, int ccNumber, float normalizedVal) noexcept;
    void handlePitchWheel(int16_t channel, float pitchBendSemis) noexcept;
    void handleClapPolyModulation(ric::mod::Dest dest, int32_t noteId, int16_t channel, int16_t key, float amount) noexcept;
    void handleClapNoteExpression(int32_t noteId, int16_t channel, int16_t key, int expressionId, double value) noexcept;

    void renderBlock(juce::AudioBuffer<float>& buffer,
                     const juce::MidiBuffer& midiMessages,
                     const EngineParameters& params,
                     ric::core::LockFreeVisualFifo& visualFifo,
                     ric::core::MidiLearnManager* midiLearn = nullptr) noexcept;

    const std::vector<TerminatedNoteEvent>& getTerminatedNotes() const noexcept { return terminatedNotes_; }
    void clearTerminatedNotes() noexcept { terminatedNotes_.clear(); }

    ric::mod::ModMatrix& getModMatrix() noexcept { return modMatrix_; }
    const ric::mod::ModMatrix& getModMatrix() const noexcept { return modMatrix_; }

private:
    SynthVoice* findVoiceToAllocate(int midiNote, int16_t channel, int maxAllowedVoices, bool& outIsLegato) noexcept;
    void renderSubBlockInternal(int startSample, int numSamples,
                                const float* extInMono,
                                float* outL, float* outR,
                                const EngineParameters& params,
                                ric::core::LockFreeVisualFifo& visualFifo) noexcept;
    void captureScopes(const float* exciter, const float* outL, const float* outR, size_t n) noexcept;

    double sampleRate_ = 48000.0;
    int currentMaxPolyphony_ = 8;
    uint64_t noteCounter_ = 0;

    // Visual telemetry (~60 frames / second, decimated scopes ~10 ms wide)
    static constexpr size_t kScopeLen = ric::core::VisualSnapshot::kScopeSamples;
    std::array<float, kScopeLen> scopeExciterRing_{};
    std::array<float, kScopeLen> scopeLeftRing_{};
    std::array<float, kScopeLen> scopeRightRing_{};
    size_t scopeWrite_ = 0;
    int scopeDecimation_ = 4;
    int scopeDecimationCounter_ = 0;
    int samplesPerFrame_ = 800;
    int samplesSinceFrame_ = 0;
    float framePeakL_ = 0.0f;
    float framePeakR_ = 0.0f;
    bool frameTrigT1_ = false;
    bool frameTrigT3_ = false;

    std::vector<float> extInputMonoBuffer_;
    std::array<SynthVoice, kMaxVoices> voices_{};
    std::array<ric::mod::PolyLFO, 3> globalLfos_{};
    CloudsWrapper clouds_;
    MarblesWrapper marbles_;
    ric::mod::ModMatrix modMatrix_;

    std::vector<TerminatedNoteEvent> terminatedNotes_;
};

} // namespace ric::dsp
