#include "SynthVoice.h"
#include <cmath>
#include <algorithm>

namespace ric::dsp {

void SynthVoice::prepare(double sampleRate, int voiceIndex) noexcept {
    voiceIndex_ = voiceIndex;
    plaits_.prepare(sampleRate);
    braids_.prepare(sampleRate);
    rings_.prepare(sampleRate);
    for (auto& e : envs_) e.prepare(sampleRate);
    reset();
}

void SynthVoice::reset() noexcept {
    isActive_ = false;
    gateOpen_ = false;
    droneFxActive_ = false;
    midiNote_ = 60;
    channel_ = 0;
    clapNoteId_ = -1;
    velocity_ = 0.8f;
    generativePitchSemis_ = 0.0f;
    effectiveNote_ = 60.0f;
    currentEnergy_ = 0.0f;
    mpePressure_ = 0.0f;
    mpeTimbre_ = 0.0f;
    mpePitchBendSemis_ = 0.0f;
    noteExprTuningSemis_ = 0.0f;
    currentSources_.fill(0.0f);
    clapPolyOffsets_.fill(0.0f);
    finalDestParams_.fill(0.0f);

    plaits_.reset();
    braids_.reset();
    rings_.reset();
    for (auto& e : envs_) e.reset();
}

void SynthVoice::noteOn(int midiNote, float velocity, int16_t channel, int32_t clapNoteId, bool legato) noexcept {
    midiNote_ = midiNote;
    velocity_ = std::clamp(velocity, 0.05f, 1.0f);
    channel_ = channel;
    clapNoteId_ = clapNoteId;
    generativePitchSemis_ = 0.0f;
    isActive_ = true;
    gateOpen_ = true;
    droneFxActive_ = false;

    if (!legato) {
        clapPolyOffsets_.fill(0.0f);
        noteExprTuningSemis_ = 0.0f;
        for (auto& e : envs_) e.noteOn();
        plaits_.triggerStrike(velocity_);
        rings_.triggerStrum(velocity_);
    }
}

void SynthVoice::noteOff(float /*velocity*/) noexcept {
    gateOpen_ = false;
    for (auto& e : envs_) e.noteOff();
}

void SynthVoice::setDroneFXActive(bool active) noexcept {
    droneFxActive_ = active;
    if (active && !isActive_) {
        isActive_ = true;
    }
}

void SynthVoice::triggerGenerativeStrum(float pitchOffsetSemis, float velocity) noexcept {
    generativePitchSemis_ = pitchOffsetSemis;
    plaits_.triggerStrike(velocity);
    rings_.triggerStrum(velocity);
    if (!gateOpen_) {
        for (auto& e : envs_) {
            e.noteOn();
            e.noteOff();
        }
    }
}

void SynthVoice::applyClapPolyMod(ric::mod::Dest dest, float offset) noexcept {
    const auto idx = static_cast<size_t>(dest);
    if (idx < clapPolyOffsets_.size()) {
        clapPolyOffsets_[idx] = offset;
    }
}

void SynthVoice::clearClapPolyMod() noexcept {
    clapPolyOffsets_.fill(0.0f);
}

bool SynthVoice::renderSubBlock(const VoiceContextParams& ctx,
                                const ric::mod::ModMatrix& modMatrix,
                                const MarblesState& marblesState,
                                const std::array<float, 3>& globalLfoValues,
                                const float* extAudioIn,
                                float* dryBusL, float* dryBusR,
                                float* cloudsSendL, float* cloudsSendR,
                                float* outScopeExciter,
                                float* outScopeOdd,
                                float* outScopeEven,
                                size_t numSamples) noexcept {
    if (!isActive_ && !droneFxActive_) return false;

    // 1. Step the 3 independent ADSR envelopes at 2kHz control rate
    std::array<float, 3> envVals{};
    for (size_t i = 0; i < 3; ++i) {
        envVals[i] = envs_[i].stepSubBlock(ctx.envs[i].attack, ctx.envs[i].decay, ctx.envs[i].sustain, ctx.envs[i].release, numSamples);
    }
    const float ampEnvVal = droneFxActive_ ? std::max(envVals[0], 0.85f) : envVals[0];
    const float subBlend = std::clamp(finalDestParams_[static_cast<size_t>(ric::mod::Dest::SubBlend)], 0.0f, 1.0f);

    // 2. Populate the 7 clear Modulation Sources (LFO 1, LFO 2, LFO 3, ENV 1, ENV 2, ENV 3, LOOM)
    using namespace ric::mod;
    currentSources_[static_cast<size_t>(Source::LFO1)] = globalLfoValues[0];
    currentSources_[static_cast<size_t>(Source::LFO2)] = globalLfoValues[1];
    currentSources_[static_cast<size_t>(Source::LFO3)] = globalLfoValues[2];
    currentSources_[static_cast<size_t>(Source::ENV1)] = ampEnvVal;
    currentSources_[static_cast<size_t>(Source::ENV2)] = envVals[1];
    currentSources_[static_cast<size_t>(Source::ENV3)] = envVals[2];
    currentSources_[static_cast<size_t>(Source::LOOM)] = marblesState.cvX1;

    // 3. Evaluate Modulation Matrix + CLAP Polyphonic Modulation Offsets
    modMatrix.evaluate(currentSources_, ctx.baseDestParams, clapPolyOffsets_, finalDestParams_);

    // 4. Compute Unified Voice & Resonator Pitch (Both track MIDI notes + pitch bend + respective Tune offsets!)
    const float oscTuneSemis   = std::clamp(finalDestParams_[static_cast<size_t>(Dest::OscTune)],   -24.0f, 24.0f);
    const float ringsTuneSemis = std::clamp(finalDestParams_[static_cast<size_t>(Dest::RingsTune)], -24.0f, 24.0f);

    // The loom's melody can move the voice, the body, both, or neither
    const bool loomToVoice = ctx.loomPitchTarget == 1 || ctx.loomPitchTarget == 2;
    const bool loomToBody  = ctx.loomPitchTarget == 1 || ctx.loomPitchTarget == 3;
    const float baseNote = static_cast<float>(midiNote_) + mpePitchBendSemis_ + noteExprTuningSemis_;
    effectiveNote_ = baseNote + oscTuneSemis + (loomToVoice ? generativePitchSemis_ : 0.0f);
    const float ringsPitchNote = baseNote + ringsTuneSemis + (loomToBody ? generativePitchSemis_ : 0.0f);

    // 5. Render Voice & Exciter Stage (Plaits + Sub/Braids Layer, coupled naturally to ENV 1)
    float plaitsMain[kSubBlockSize]{};
    float plaitsAux[kSubBlockSize]{};
    float braidsOut[kSubBlockSize]{};
    float excitationIn[kSubBlockSize]{};

    const bool runInternalOsc = !ctx.exciterBypass && (ctx.inputSource != 1);
    if (runInternalOsc) {
        PlaitsPatch pPatch{};
        pPatch.engine    = ctx.oscEngine;
        pPatch.note      = effectiveNote_;
        pPatch.harmonics = std::clamp(finalDestParams_[static_cast<size_t>(Dest::OscHarmonics)], 0.0f, 1.0f);
        pPatch.timbre    = std::clamp(finalDestParams_[static_cast<size_t>(Dest::OscTimbre)] + mpeTimbre_ * 0.35f, 0.0f, 1.0f);
        pPatch.morph     = std::clamp(finalDestParams_[static_cast<size_t>(Dest::OscMorph)], 0.0f, 1.0f);
        // Couple internal LPG smoothly to ENV 1 so there are no redundant LPG knobs
        pPatch.lpgColour = std::clamp(0.35f + ampEnvVal * 0.55f, 0.0f, 1.0f);
        pPatch.lpgDecay  = std::clamp(0.45f + ctx.envs[0].decay * 0.1f, 0.1f, 0.95f);

        plaits_.renderSubBlock(pPatch, gateOpen_, plaitsMain, plaitsAux, numSamples);

        if (subBlend > 0.001f) {
            braids_.renderSubBlock(ctx.oscEngine, effectiveNote_ - 12.0f, pPatch.timbre, pPatch.harmonics, braidsOut, numSamples);
        }
    }

    const float envGate = std::max(ampEnvVal, gateOpen_ ? 0.12f : 0.0f);
    const bool useExtAudio = (ctx.inputSource == 1 || ctx.inputSource == 2) && (extAudioIn != nullptr);

    for (size_t i = 0; i < numSamples; ++i) {
        float combinedOsc = 0.0f;
        if (runInternalOsc) {
            combinedOsc = (plaitsMain[i] * (1.0f - subBlend * 0.40f)
                         + braidsOut[i] * (subBlend * 0.80f)
                         + plaitsAux[i] * 0.18f) * envGate;
        }
        if (useExtAudio) {
            combinedOsc += extAudioIn[i];
        }
        excitationIn[i] = combinedOsc * 0.85f;
    }

    // 6. Render Resonator Body (Rings)
    float ringsOdd[kSubBlockSize]{};
    float ringsEven[kSubBlockSize]{};

    const float strikeVal = std::clamp(finalDestParams_[static_cast<size_t>(Dest::StrikeLevel)], 0.0f, 1.0f);
    if (!ctx.ringsBypass) {
        RingsPatch rPatch{};
        rPatch.model           = ctx.ringsModel;
        rPatch.polyphony       = ctx.ringsPoly;
        rPatch.frequency       = ringsPitchNote;
        rPatch.structure       = std::clamp(finalDestParams_[static_cast<size_t>(Dest::RingsStructure)],  0.0f, 0.995f);
        rPatch.brightness      = std::clamp(finalDestParams_[static_cast<size_t>(Dest::RingsBrightness)] + mpePressure_ * 0.35f, 0.0f, 1.0f);
        rPatch.damping         = std::clamp(finalDestParams_[static_cast<size_t>(Dest::RingsDamping)],    0.0f, 0.998f);
        rPatch.position        = std::clamp(finalDestParams_[static_cast<size_t>(Dest::RingsPosition)],   0.02f, 0.98f);
        // When Exciter is bypassed, boost internal strike so Rings always sounds immediately from MIDI
        rPatch.internalExciter = (ctx.exciterBypass && ctx.inputSource == 0) ? std::max(0.65f, strikeVal) : strikeVal;

        rings_.processSubBlock(rPatch, excitationIn, ringsOdd, ringsEven, numSamples);
    }

    // 7. Combine Direct Exciter Mix + Resonator Body -> Cloud & Space or Master Dry Bus
    const float directMix = std::clamp(finalDestParams_[static_cast<size_t>(Dest::DirectMix)], 0.0f, 1.0f);
    const float panOffset = (voiceIndex_ % 2 == 0) ? 0.42f : 0.58f;
    const float panL = std::cos(panOffset * stmlib::kPi * 0.5f) * 1.3f;
    const float panR = std::sin(panOffset * stmlib::kPi * 0.5f) * 1.3f;

    const float voiceVca = droneFxActive_
        ? 1.0f
        : std::clamp(ampEnvVal * 0.85f + (rings_.getRmsEnergy() > 0.0005f ? 0.45f * (gateOpen_ ? 1.0f : ampEnvVal + 0.35f) : 0.0f), 0.0f, 1.25f);

    float blockEnergy = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float vL = 0.0f;
        float vR = 0.0f;

        if (ctx.ringsBypass) {
            vL = excitationIn[i] * panL * voiceVca;
            vR = excitationIn[i] * panR * voiceVca;
        } else {
            vL = (ringsOdd[i] * (1.0f - directMix * 0.3f) + excitationIn[i] * directMix * 0.85f) * panL * voiceVca;
            vR = (ringsEven[i] * (1.0f - directMix * 0.3f) + excitationIn[i] * directMix * 0.85f) * panR * voiceVca;
        }

        if (ctx.cloudsBypass) {
            dryBusL[i] += vL;
            dryBusR[i] += vR;
        } else {
            cloudsSendL[i] += vL;
            cloudsSendR[i] += vR;
        }

        if (outScopeExciter) outScopeExciter[i] += excitationIn[i] * voiceVca;
        if (outScopeOdd)     outScopeOdd[i]     += vL;
        if (outScopeEven)    outScopeEven[i]    += vR;

        blockEnergy += vL * vL + vR * vR;
    }

    currentEnergy_ = std::sqrt(blockEnergy / static_cast<float>(numSamples * 2));

    if (!droneFxActive_ && !gateOpen_ && !envs_[0].isActive() && currentEnergy_ < 0.00015f) {
        isActive_ = false;
        return true;
    }

    return false;
}

} // namespace ric::dsp
