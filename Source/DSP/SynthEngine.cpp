#include "SynthEngine.h"
#include "../Core/SubBlockAdapter.h"
#include <clap/events.h>
#include <cmath>
#include <algorithm>

namespace ric::dsp {

void SynthEngine::prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    scopeDecimation_ = std::max(1, static_cast<int>(std::lround(sampleRate / 12000.0)));
    samplesPerFrame_ = std::max(64, static_cast<int>(sampleRate / 60.0));
    extInputMonoBuffer_.assign(static_cast<size_t>(std::max(4096, maxBlockSize * 2)), 0.0f);
    for (size_t i = 0; i < kMaxVoices; ++i) {
        voices_[i].prepare(sampleRate, static_cast<int>(i));
    }
    globalLfos_[0].prepare(sampleRate, 0.0f);
    globalLfos_[1].prepare(sampleRate, 0.33f);
    globalLfos_[2].prepare(sampleRate, 0.67f);
    clouds_.prepare(sampleRate);
    marbles_.prepare(sampleRate);
    terminatedNotes_.reserve(64);
    reset();
}

void SynthEngine::reset() noexcept {
    for (auto& v : voices_) v.reset();
    globalLfos_[0].reset(0.0f);
    globalLfos_[1].reset(0.33f);
    globalLfos_[2].reset(0.67f);
    clouds_.reset();
    marbles_.reset();
    terminatedNotes_.clear();
    noteCounter_ = 0;
    scopeExciterRing_.fill(0.0f);
    scopeLeftRing_.fill(0.0f);
    scopeRightRing_.fill(0.0f);
    scopeWrite_ = 0;
    scopeDecimationCounter_ = 0;
    samplesSinceFrame_ = 0;
    framePeakL_ = framePeakR_ = 0.0f;
    frameTrigT1_ = frameTrigT3_ = false;
}

void SynthEngine::captureScopes(const float* exciter, const float* outL, const float* outR, size_t n) noexcept {
    for (size_t i = 0; i < n; ++i) {
        framePeakL_ = std::max(framePeakL_, std::abs(outL[i]));
        framePeakR_ = std::max(framePeakR_, std::abs(outR[i]));
        if (++scopeDecimationCounter_ < scopeDecimation_) continue;
        scopeDecimationCounter_ = 0;
        scopeExciterRing_[scopeWrite_] = exciter[i];
        scopeLeftRing_[scopeWrite_] = outL[i];
        scopeRightRing_[scopeWrite_] = outR[i];
        scopeWrite_ = (scopeWrite_ + 1) % kScopeLen;
    }
}

SynthVoice* SynthEngine::findVoiceToAllocate(int midiNote, int16_t channel, int maxAllowedVoices, bool& outIsLegato) noexcept {
    outIsLegato = false;
    const size_t limit = static_cast<size_t>(std::clamp(maxAllowedVoices, 1, static_cast<int>(kMaxVoices)));

    if (limit == 1) {
        outIsLegato = voices_[0].isActive() && voices_[0].isGateOpen();
        return &voices_[0];
    }

    for (size_t i = 0; i < limit; ++i) {
        if (voices_[i].isActive() && voices_[i].getMidiNote() == midiNote && voices_[i].getChannel() == channel) {
            return &voices_[i];
        }
    }

    for (size_t i = 0; i < limit; ++i) {
        if (!voices_[i].isActive()) {
            return &voices_[i];
        }
    }

    SynthVoice* bestReleasing = nullptr;
    float minEnergy = 99999.0f;
    SynthVoice* oldestVoice = &voices_[0];
    uint64_t oldestTimestamp = voices_[0].getNoteOnCounter();

    for (size_t i = 0; i < limit; ++i) {
        auto& v = voices_[i];
        if (!v.isGateOpen() && v.getVoiceEnergy() < minEnergy) {
            minEnergy = v.getVoiceEnergy();
            bestReleasing = &v;
        }
        if (v.getNoteOnCounter() < oldestTimestamp) {
            oldestTimestamp = v.getNoteOnCounter();
            oldestVoice = &v;
        }
    }

    return bestReleasing ? bestReleasing : oldestVoice;
}

SynthVoice* SynthEngine::handleNoteOn(int midiNote, float velocity, int16_t channel, int32_t clapNoteId) noexcept {
    bool isLegato = false;
    SynthVoice* v = findVoiceToAllocate(midiNote, channel, currentMaxPolyphony_, isLegato);
    if (!v) return nullptr;

    if (v->isActive() && !isLegato && v->getClapNoteId() >= 0) {
        terminatedNotes_.push_back({ v->getClapNoteId(), 0, v->getChannel(), static_cast<int16_t>(v->getMidiNote()), 0 });
    }

    v->setNoteOnCounter(++noteCounter_);
    v->noteOn(midiNote, velocity, channel, clapNoteId, isLegato);
    return v;
}

void SynthEngine::handleNoteOff(int midiNote, float velocity, int16_t channel, int32_t clapNoteId) noexcept {
    for (auto& v : voices_) {
        if (!v.isActive() || !v.isGateOpen()) continue;
        const bool matchId = (clapNoteId >= 0 && v.getClapNoteId() == clapNoteId);
        const bool matchNote = (v.getMidiNote() == midiNote && (channel < 0 || v.getChannel() == channel));
        if (matchId || matchNote) {
            v.noteOff(velocity);
        }
    }
}

void SynthEngine::handleAllNotesOff() noexcept {
    for (auto& v : voices_) {
        if (v.isActive()) {
            v.noteOff(0.0f);
        }
    }
}

void SynthEngine::handleChannelPressure(int16_t channel, float pressure) noexcept {
    for (auto& v : voices_) {
        if (v.isActive() && (channel < 0 || v.getChannel() == channel || channel == 0)) {
            v.setMpePressure(pressure);
        }
    }
}

void SynthEngine::handleController(int16_t channel, int ccNumber, float normalizedVal) noexcept {
    if (ccNumber == 74 || ccNumber == 1) {
        for (auto& v : voices_) {
            if (v.isActive() && (channel < 0 || v.getChannel() == channel || channel == 0)) {
                v.setMpeTimbre(normalizedVal);
            }
        }
    } else if (ccNumber == 123 || ccNumber == 120) {
        handleAllNotesOff();
    }
}

void SynthEngine::handlePitchWheel(int16_t channel, float pitchBendSemis) noexcept {
    for (auto& v : voices_) {
        if (v.isActive() && (channel < 0 || v.getChannel() == channel || channel == 0)) {
            v.setMpePitchBend(pitchBendSemis);
        }
    }
}

void SynthEngine::handleClapPolyModulation(ric::mod::Dest dest, int32_t noteId, int16_t channel, int16_t key, float amount) noexcept {
    for (auto& v : voices_) {
        if (!v.isActive()) continue;
        const bool idMatch = (noteId >= 0 && v.getClapNoteId() == noteId);
        const bool keyMatch = (noteId < 0 && key >= 0 && v.getMidiNote() == key && (channel < 0 || v.getChannel() == channel));
        if (idMatch || keyMatch) {
            v.applyClapPolyMod(dest, amount);
        }
    }
}

void SynthEngine::handleClapNoteExpression(int32_t noteId, int16_t channel, int16_t key, int expressionId, double value) noexcept {
    for (auto& v : voices_) {
        if (!v.isActive()) continue;
        const bool idMatch = (noteId >= 0 && v.getClapNoteId() == noteId);
        const bool keyMatch = (noteId < 0 && key >= 0 && v.getMidiNote() == key && (channel < 0 || v.getChannel() == channel));
        if (!idMatch && !keyMatch) continue;

        switch (expressionId) {
            case CLAP_NOTE_EXPRESSION_TUNING:
                v.setNoteExpressionTuning(static_cast<float>(value));
                break;
            case CLAP_NOTE_EXPRESSION_BRIGHTNESS:
                v.setMpeTimbre(static_cast<float>(value));
                break;
            case CLAP_NOTE_EXPRESSION_PRESSURE:
            case CLAP_NOTE_EXPRESSION_VOLUME:
                v.setMpePressure(static_cast<float>(value));
                break;
            default:
                break;
        }
    }
}

void SynthEngine::renderBlock(juce::AudioBuffer<float>& buffer,
                              const juce::MidiBuffer& midiMessages,
                              const EngineParameters& params,
                              ric::core::LockFreeVisualFifo& visualFifo,
                              ric::core::MidiLearnManager* midiLearn) noexcept {
    currentMaxPolyphony_ = params.maxPolyphony;
    const int totalSamples = buffer.getNumSamples();
    if (totalSamples <= 0) return;

    if (static_cast<size_t>(totalSamples) > extInputMonoBuffer_.size()) {
        extInputMonoBuffer_.resize(static_cast<size_t>(totalSamples), 0.0f);
    }
    const bool useExtAudio = (params.voiceCtx.inputSource == 1 || params.voiceCtx.inputSource == 2);
    if (useExtAudio && buffer.getNumChannels() > 0) {
        const float* inL = buffer.getReadPointer(0);
        const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inL;
        for (int i = 0; i < totalSamples; ++i) {
            extInputMonoBuffer_[static_cast<size_t>(i)] = (inL[i] + inR[i]) * 0.5f;
        }
    } else {
        std::fill_n(extInputMonoBuffer_.begin(), totalSamples, 0.0f);
    }

    if (useExtAudio) {
        bool anyPlaying = false;
        for (const auto& v : voices_) {
            if (v.isActive() && !v.isDroneFx()) { anyPlaying = true; break; }
        }
        voices_[0].setDroneFXActive(!anyPlaying);
    } else {
        for (auto& v : voices_) v.setDroneFXActive(false);
    }

    buffer.clear();

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;

    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    int currentSample = 0;
    while (currentSample < totalSamples) {
        while (midiIterator != midiEnd && (*midiIterator).samplePosition <= currentSample) {
            const auto msg = (*midiIterator).getMessage();
            const int16_t ch = static_cast<int16_t>(msg.getChannel() - 1);
            if (msg.isNoteOn()) {
                handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity(), ch, -1);
            } else if (msg.isNoteOff()) {
                handleNoteOff(msg.getNoteNumber(), msg.getFloatVelocity(), ch, -1);
            } else if (msg.isChannelPressure()) {
                handleChannelPressure(ch, static_cast<float>(msg.getChannelPressureValue()) / 127.0f);
            } else if (msg.isAftertouch()) {
                handleClapNoteExpression(-1, ch, static_cast<int16_t>(msg.getNoteNumber()),
                                         CLAP_NOTE_EXPRESSION_PRESSURE,
                                         static_cast<double>(msg.getAfterTouchValue()) / 127.0);
            } else if (msg.isController()) {
                const float normVal = static_cast<float>(msg.getControllerValue()) / 127.0f;
                if (midiLearn != nullptr) {
                    midiLearn->processMidiCC(msg.getControllerNumber(), normVal);
                }
                handleController(ch, msg.getControllerNumber(), normVal);
            } else if (msg.isPitchWheel()) {
                const float normBend = (static_cast<float>(msg.getPitchWheelValue()) - 8192.0f) / 8192.0f;
                const float bendRange = (ch > 0) ? 12.0f : 2.0f;
                handlePitchWheel(ch, normBend * bendRange);
            }
            ++midiIterator;
        }

        int nextEventSample = totalSamples;
        if (midiIterator != midiEnd) {
            nextEventSample = std::min(totalSamples, std::max(currentSample + 1, (*midiIterator).samplePosition));
        }
        const int chunk = std::min(static_cast<int>(kSubBlockSize), nextEventSample - currentSample);

        renderSubBlockInternal(currentSample, chunk,
                               extInputMonoBuffer_.data() + currentSample,
                               outL + currentSample, outR + currentSample,
                               params, visualFifo);
        currentSample += chunk;
    }
}

void SynthEngine::renderSubBlockInternal(int startSample, int numSamples,
                                         const float* extInMono,
                                         float* outL, float* outR,
                                         const EngineParameters& params,
                                         ric::core::LockFreeVisualFifo& visualFifo) noexcept {
    const size_t n = static_cast<size_t>(numSamples);

    // 1. Always step the 3 independent LFOs and the Generative Loom so live modulation rings animate even before a key is pressed
    std::array<float, 3> lfoVals{};
    const double beatsPerSecond = std::max(20.0, params.hostBpm) / 60.0;
    for (size_t i = 0; i < 3; ++i) {
        const auto& vc = params.voiceCtx;
        if (!vc.lfoSync[i]) {
            lfoVals[i] = globalLfos_[i].stepSubBlock(vc.lfoRates[i], n, vc.lfoShapes[i]);
        } else if (params.hostPlaying) {
            // Locked to the bar: phase comes straight from the host's song position
            const double ppq = params.hostPpqAtBlockStart + static_cast<double>(startSample) / sampleRate_ * beatsPerSecond;
            lfoVals[i] = globalLfos_[i].stepToPhase(static_cast<float>(std::fmod(ppq / vc.lfoBeats[i], 1.0)), vc.lfoShapes[i]);
        } else {
            lfoVals[i] = globalLfos_[i].stepSubBlock(static_cast<float>(beatsPerSecond / vc.lfoBeats[i]), n, vc.lfoShapes[i]);
        }
    }
    const MarblesState mState = marbles_.stepSubBlock(params.marbles, n);
    frameTrigT1_ = frameTrigT1_ || mState.trigT1;
    frameTrigT3_ = frameTrigT3_ || mState.trigT3;

    if (params.marbles.enabled && (mState.trigT1 || mState.trigT3)) {
        const float pitchOffset = MarblesWrapper::quantizeToScaleSemitones(mState.cvX1, params.marbles.spread * 16.0f);
        bool triggeredAny = false;
        const size_t limit = static_cast<size_t>(std::clamp(params.maxPolyphony, 1, static_cast<int>(kMaxVoices)));

        for (size_t i = 0; i < limit; ++i) {
            if (voices_[i].isActive() && voices_[i].isGateOpen()) {
                voices_[i].triggerGenerativeStrum(pitchOffset, 0.55f + 0.35f * std::abs(mState.cvX2));
                triggeredAny = true;
            }
        }
        if (!triggeredAny && mState.trigT1) {
            // With nothing held, the loom plays its own note around middle C;
            // where its melody goes is decided per voice by the pitch target
            constexpr int kLoomNote = 60;
            if (auto* v = handleNoteOn(kLoomNote, 0.65f + 0.25f * std::abs(mState.cvX2), 0, -1))
                v->setGenerativePitch(pitchOffset);
            handleNoteOff(kLoomNote, 0.0f, 0, -1);
        }
        if (mState.trigT3) {
            clouds_.triggerGrainSeed();
        }
    }

    // 2. Render all active polyphonic voices
    float dryBusL[kSubBlockSize]{};
    float dryBusR[kSubBlockSize]{};
    float cloudsSendL[kSubBlockSize]{};
    float cloudsSendR[kSubBlockSize]{};
    float scopeExciter[kSubBlockSize]{};

    float totalResonatorEnergy = 0.0f;
    const SynthVoice* leadVoice = nullptr;

    for (size_t i = 0; i < kMaxVoices; ++i) {
        auto& v = voices_[i];
        if (!v.isActive()) continue;

        const bool ended = v.renderSubBlock(params.voiceCtx, modMatrix_, mState, lfoVals,
                                            extInMono,
                                            dryBusL, dryBusR, cloudsSendL, cloudsSendR,
                                            scopeExciter, nullptr, nullptr, n);
        if (ended) {
            if (v.getClapNoteId() >= 0) {
                terminatedNotes_.push_back({ v.getClapNoteId(), 0, v.getChannel(),
                                             static_cast<int16_t>(v.getMidiNote()),
                                             static_cast<uint32_t>(startSample) });
            }
        } else {
            totalResonatorEnergy += v.getVoiceEnergy();
            if (!leadVoice || v.getNoteOnCounter() > leadVoice->getNoteOnCounter()) {
                leadVoice = &v;
            }
        }
    }

    // 3. Evaluate global/idle modulation so Clouds and UI knob rings always show live LFO/Loom motion!
    std::array<float, ric::mod::ModMatrix::kNumSources> liveSources{};
    std::array<float, ric::mod::ModMatrix::kNumDests> liveFinalDests{};
    if (leadVoice) {
        liveSources = leadVoice->getModSources();
        for (size_t d = 0; d < ric::mod::ModMatrix::kNumDests; ++d) {
            liveFinalDests[d] = leadVoice->getModulatedDest(static_cast<ric::mod::Dest>(d));
        }
    } else {
        liveSources[static_cast<size_t>(ric::mod::Source::LFO1)] = lfoVals[0];
        liveSources[static_cast<size_t>(ric::mod::Source::LFO2)] = lfoVals[1];
        liveSources[static_cast<size_t>(ric::mod::Source::LFO3)] = lfoVals[2];
        liveSources[static_cast<size_t>(ric::mod::Source::ENV1)] = 0.0f;
        liveSources[static_cast<size_t>(ric::mod::Source::ENV2)] = 0.0f;
        liveSources[static_cast<size_t>(ric::mod::Source::ENV3)] = 0.0f;
        liveSources[static_cast<size_t>(ric::mod::Source::LOOM)] = mState.cvX1;

        std::array<float, ric::mod::ModMatrix::kNumDests> zeroPoly{};
        modMatrix_.evaluate(liveSources, params.voiceCtx.baseDestParams, zeroPoly, liveFinalDests);
    }

    // 4. Process Clouds Granular & Oliverb Diffusion Engine (unless bypassed)
    using namespace ric::mod;
    CloudsPatch cPatch = params.clouds;
    cPatch.position = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsPosition)], 0.0f, 0.995f);
    cPatch.size     = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsSize)],     0.0f, 1.0f);
    cPatch.density  = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsDensity)],  0.0f, 1.0f);
    cPatch.texture  = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsTexture)],  0.0f, 1.0f);
    cPatch.pitch    = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsPitch)],   -24.0f, 24.0f);
    cPatch.feedback = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsFeedback)], 0.0f, 0.95f);
    cPatch.reverb   = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsReverb)],   0.0f, 1.0f);
    cPatch.dryWet   = std::clamp(liveFinalDests[static_cast<size_t>(Dest::CloudsDryWet)],   0.0f, 1.0f);

    float cloudsOutL[kSubBlockSize]{};
    float cloudsOutR[kSubBlockSize]{};

    if (!params.voiceCtx.cloudsBypass) {
        clouds_.processSubBlock(cPatch, cloudsSendL, cloudsSendR, cloudsOutL, cloudsOutR, n);
    }

    // 5. Master Output Summing & Soft Limiter
    const float masterGain = params.masterGainLinear;
    for (size_t i = 0; i < n; ++i) {
        outL[i] = stmlib::SoftClip((dryBusL[i] + cloudsOutL[i]) * masterGain);
        outR[i] = stmlib::SoftClip((dryBusR[i] + cloudsOutR[i]) * masterGain);
    }

    // 6. Visual telemetry: capture scopes every sub-block, publish a frame ~60 times per second
    captureScopes(scopeExciter, outL, outR, n);
    samplesSinceFrame_ += numSamples;
    if (samplesSinceFrame_ < samplesPerFrame_)
        return;
    samplesSinceFrame_ = 0;

    ric::core::VisualSnapshot snap{};
    for (size_t i = 0; i < kScopeLen; ++i) {
        const size_t idx = (scopeWrite_ + i) % kScopeLen; // oldest -> newest
        snap.exciterWave[i] = scopeExciterRing_[idx];
        snap.outLeft[i]     = scopeLeftRing_[idx];
        snap.outRight[i]    = scopeRightRing_[idx];
    }
    snap.peakLeft = framePeakL_;
    snap.peakRight = framePeakR_;
    framePeakL_ = framePeakR_ = 0.0f;

    for (size_t d = 0; d < ModMatrix::kNumDests; ++d) {
        const auto destEnum = static_cast<Dest>(d);
        snap.modulatedNormalized[d] = isPitchDestination(destEnum)
            ? std::clamp((liveFinalDests[d] + 24.0f) / 48.0f, 0.0f, 1.0f)
            : std::clamp(liveFinalDests[d], 0.0f, 1.0f);
    }
    snap.modSourceValues = liveSources;

    uint8_t activeVoiceCount = 0;
    float exciterEnergy = 0.0f;
    for (size_t i = 0; i < kScopeLen; ++i) exciterEnergy += snap.exciterWave[i] * snap.exciterWave[i];
    snap.exciterEnergy = std::sqrt(exciterEnergy / static_cast<float>(kScopeLen));
    for (size_t i = 0; i < kMaxVoices; ++i) {
        const auto& v = voices_[i];
        auto& vv = snap.voices[i];
        vv.active = v.isActive();
        vv.gate = v.isGateOpen();
        vv.note = v.getEffectivePitchNote();
        vv.energy = v.isActive() ? v.getVoiceEnergy() : 0.0f;
        if (vv.active) ++activeVoiceCount;
    }
    snap.activeVoices = activeVoiceCount;
    snap.resonatorEnergy = std::min(1.5f, totalResonatorEnergy);
    snap.leadNote = leadVoice ? leadVoice->getEffectivePitchNote() : 60.0f;

    clouds_.fillVisualTelemetry(snap, cPatch);

    snap.loomEnabled = params.marbles.enabled;
    snap.loomStep = mState.currentStep;
    snap.loomLength = static_cast<uint8_t>(std::clamp(params.marbles.loopLength, 1, 16));
    snap.loomTrigT1 = frameTrigT1_;
    snap.loomTrigT3 = frameTrigT3_;
    frameTrigT1_ = frameTrigT3_ = false;
    snap.loomStepsX1 = marbles_.getLoopX1();
    snap.loomX1 = mState.cvX1;
    snap.loomY = mState.cvY;

    visualFifo.push(snap);
}

} // namespace ric::dsp
