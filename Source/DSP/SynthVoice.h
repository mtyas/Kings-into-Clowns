#pragma once
#include "Wrappers/PlaitsWrapper.h"
#include "Wrappers/BraidsWrapper.h"
#include "Wrappers/RingsWrapper.h"
#include "Wrappers/MarblesWrapper.h"
#include "Modulation/EnvGenerator.h"
#include "Modulation/PolyLFO.h"
#include "Modulation/ModMatrix.h"
#include <array>
#include <cstdint>

namespace ric::dsp {

struct ADSRParams {
    float attack = 0.015f;
    float decay = 1.5f;
    float sustain = 0.5f;
    float release = 2.5f;
};

struct VoiceContextParams {
    bool exciterBypass = false;
    bool ringsBypass = false;
    bool cloudsBypass = false;

    int inputSource = 0;      // 0=Internal Synth, 1=External Audio FX, 2=Synth + External Audio
    int oscEngine = 0;
    int ringsModel = 0;
    int ringsPoly = 1;

    std::array<float, 3> lfoRates{ 0.35f, 0.22f, 0.14f };
    std::array<int, 3> lfoShapes{ 0, 1, 0 };
    std::array<bool, 3> lfoSync{ false, false, false };
    std::array<float, 3> lfoBeats{ 1.0f, 1.0f, 1.0f };   // cycle length in quarter notes when synced
    int loomPitchTarget = 1;                             // 0 rhythm only, 1 voice & body, 2 voice, 3 body
    std::array<ADSRParams, 3> envs{};

    std::array<float, ric::mod::ModMatrix::kNumDests> baseDestParams{};
};

class SynthVoice {
public:
    static constexpr size_t kSubBlockSize = 24;

    void prepare(double sampleRate, int voiceIndex) noexcept;
    void reset() noexcept;

    void noteOn(int midiNote, float velocity, int16_t channel, int32_t clapNoteId, bool legato = false) noexcept;
    void noteOff(float velocity) noexcept;
    void triggerGenerativeStrum(float pitchOffsetSemis, float velocity = 0.75f) noexcept;
    void setGenerativePitch(float semis) noexcept { generativePitchSemis_ = semis; }
    void setDroneFXActive(bool active) noexcept;

    // CLAP per-note modulation & MPE expression hooks
    void applyClapPolyMod(ric::mod::Dest dest, float offset) noexcept;
    void clearClapPolyMod() noexcept;
    void setMpePressure(float pressure) noexcept { mpePressure_ = pressure; }
    void setMpeTimbre(float timbre) noexcept { mpeTimbre_ = timbre; }
    void setMpePitchBend(float semitones) noexcept { mpePitchBendSemis_ = semitones; }
    void setNoteExpressionTuning(float semitones) noexcept { noteExprTuningSemis_ = semitones; }

    // Renders up to 24 samples into dryBus and cloudsSendBus, returning true if voice just finished
    bool renderSubBlock(const VoiceContextParams& ctx,
                        const ric::mod::ModMatrix& modMatrix,
                        const MarblesState& marblesState,
                        const std::array<float, 3>& globalLfoValues,
                        const float* extAudioIn,
                        float* dryBusL, float* dryBusR,
                        float* cloudsSendL, float* cloudsSendR,
                        float* outScopeExciter,
                        float* outScopeOdd,
                        float* outScopeEven,
                        size_t numSamples) noexcept;

    bool isActive() const noexcept { return isActive_; }
    bool isDroneFx() const noexcept { return droneFxActive_; }
    bool isGateOpen() const noexcept { return gateOpen_; }
    int getMidiNote() const noexcept { return midiNote_; }
    int16_t getChannel() const noexcept { return channel_; }
    int32_t getClapNoteId() const noexcept { return clapNoteId_; }
    uint64_t getNoteOnCounter() const noexcept { return noteOnTimestamp_; }
    void setNoteOnCounter(uint64_t t) noexcept { noteOnTimestamp_ = t; }
    float getVoiceEnergy() const noexcept { return currentEnergy_; }
    float getEffectivePitchNote() const noexcept { return effectiveNote_; }
    float getModulatedDest(ric::mod::Dest d) const noexcept {
        return finalDestParams_[static_cast<size_t>(d)];
    }
    const std::array<float, ric::mod::ModMatrix::kNumSources>& getModSources() const noexcept {
        return currentSources_;
    }

private:
    int voiceIndex_ = 0;
    bool isActive_ = false;
    bool gateOpen_ = false;
    bool droneFxActive_ = false;
    int midiNote_ = 60;
    int16_t channel_ = 0;
    int32_t clapNoteId_ = -1;
    uint64_t noteOnTimestamp_ = 0;
    float velocity_ = 0.8f;
    float generativePitchSemis_ = 0.0f;
    float effectiveNote_ = 60.0f;
    float currentEnergy_ = 0.0f;

    float mpePressure_ = 0.0f;
    float mpeTimbre_ = 0.0f;
    float mpePitchBendSemis_ = 0.0f;
    float noteExprTuningSemis_ = 0.0f;

    PlaitsWrapper plaits_;
    BraidsWrapper braids_;
    RingsWrapper rings_;

    std::array<ric::mod::EnvGenerator, 3> envs_{};

    std::array<float, ric::mod::ModMatrix::kNumSources> currentSources_{};
    std::array<float, ric::mod::ModMatrix::kNumDests> clapPolyOffsets_{};
    std::array<float, ric::mod::ModMatrix::kNumDests> finalDestParams_{};
};

} // namespace ric::dsp
