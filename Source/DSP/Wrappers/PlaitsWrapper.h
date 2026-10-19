#pragma once
#include "../../Core/StmDspCore.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace ric::dsp {

struct PlaitsPatch {
    int engine = 0;         // 0..11
    float note = 60.0f;     // MIDI note
    float harmonics = 0.5f; // [0..1]
    float timbre = 0.5f;    // [0..1]
    float morph = 0.5f;     // [0..1]
    float lpgColour = 0.5f; // [0..1]
    float lpgDecay = 0.5f;  // [0..1]
};

// Multi-Engine Macro Oscillator & Vactrol Low-Pass Gate (Plaits DSP Architecture)
class PlaitsWrapper {
public:
    static constexpr size_t kMaxBlockSize = 24;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        invSampleRate_ = 1.0f / sampleRate_;
        rng_.Init(0x7461A893u);
        svfLpg_.Init();
        svfAux_.Init();
        dcBlocker_.Init(0.9993f);
        reset();
    }

    void reset() noexcept {
        phase1_ = 0.0f;
        phase2_ = 0.0f;
        phase3_ = 0.0f;
        chordPhases_.fill(0.0f);
        additivePhases_.fill(0.0f);
        lpgEnv_ = 0.0f;
        strikeEnv_ = 0.0f;
        dustPhase_ = 0.0f;
        ksWritePtr_ = 0;
        ksDelayLine_.fill(0.0f);
        ksFilterState_ = 0.0f;
        prevHarmonics_ = 0.5f;
        prevTimbre_ = 0.5f;
        prevMorph_ = 0.5f;
        svfLpg_.Reset();
        svfAux_.Reset();
    }

    void triggerStrike(float velocity = 1.0f) noexcept {
        lpgEnv_ = std::max(lpgEnv_, std::clamp(velocity, 0.1f, 1.0f));
        strikeEnv_ = 1.0f;

        // Pre-excite Karplus-Strong delay line with filtered noise burst
        for (size_t i = 0; i < ksDelayLine_.size(); ++i) {
            ksDelayLine_[i] = rng_.GetBipolar() * velocity * 0.7f;
        }
    }

    // Renders up to 24 samples of mainOut and auxOut
    void renderSubBlock(const PlaitsPatch& patch, bool gateOpen, float* mainOut, float* auxOut, size_t size) noexcept {
        const float freq = std::clamp(stmlib::MidiToFrequency(patch.note), 16.0f, sampleRate_ * 0.45f);
        const float f0 = freq * invSampleRate_;

        stmlib::ParameterInterpolator harmMod(&prevHarmonics_, std::clamp(patch.harmonics, 0.0f, 1.0f), size);
        stmlib::ParameterInterpolator timbMod(&prevTimbre_, std::clamp(patch.timbre, 0.0f, 1.0f), size);
        stmlib::ParameterInterpolator morphMod(&prevMorph_, std::clamp(patch.morph, 0.0f, 1.0f), size);

        // Vactrol LPG Decay coefficient
        const float decayTimeSec = 0.015f * std::pow(180.0f, std::clamp(patch.lpgDecay, 0.0f, 1.0f));
        const float decayCoeff = std::exp(-1.0f / (decayTimeSec * sampleRate_));
        const float strikeDecay = std::exp(-1.0f / (0.018f * sampleRate_));

        // Buchla-style Low-Pass Gate (simultaneous VCA + SVF low-pass); the vactrol is slow
        // enough that control-rate tuning (~2 kHz) is inaudible.
        const float lpgBlockAmount = std::clamp(lpgEnv_ + (gateOpen ? 0.35f : 0.0f), 0.0f, 1.0f);
        const float lpgCutoff = std::clamp(f0 * (1.5f + 28.0f * lpgBlockAmount * patch.lpgColour + 10.0f * (1.0f - patch.lpgColour)), 0.002f, 0.48f);
        svfLpg_.set_f_q(lpgCutoff, 0.707f + 1.2f * patch.lpgColour * (1.0f - lpgBlockAmount));

        for (size_t i = 0; i < size; ++i) {
            const float h = harmMod.Next();
            const float t = timbMod.Next();
            const float m = morphMod.Next();

            // Update Vactrol LPG envelope
            if (gateOpen && patch.lpgDecay > 0.72f) {
                const float sustainTarget = (patch.lpgDecay - 0.72f) / 0.28f;
                lpgEnv_ = std::max(lpgEnv_ * decayCoeff, sustainTarget);
            } else {
                lpgEnv_ *= decayCoeff;
            }
            strikeEnv_ *= strikeDecay;

            float sigMain = 0.0f;
            float sigAux = 0.0f;

            switch (patch.engine) {
                case 0: // I. Virtual Analog (Detuned Saw + Variable Pulse + Sync)
                    renderVirtualAnalog(f0, h, t, m, sigMain, sigAux);
                    break;
                case 1: // II. West-Coast Waveshaper & Buchla Asymmetric Wavefolder
                    renderWaveshaper(f0, h, t, m, sigMain, sigAux);
                    break;
                case 2: // III. Two-Operator Phase Modulation FM with Feedback
                    renderTwoOpFM(f0, h, t, m, sigMain, sigAux);
                    break;
                case 3: // IV. VOSIM Formant & Speech Vowel Synthesizer
                    renderFormantSpeech(f0, h, t, m, sigMain, sigAux);
                    break;
                case 4: // V. 24-HarmonicChebyshev Additive Organ
                    renderHarmonicAdditive(f0, h, t, m, sigMain, sigAux);
                    break;
                case 5: // VI. 2D Morphing Wavetable Bank
                    renderWavetable(f0, h, t, m, sigMain, sigAux);
                    break;
                case 6: // VII. Solina Chord & Divide-Down String Ensemble
                    renderChordMachine(f0, h, t, m, sigMain, sigAux);
                    break;
                case 7: // VIII. Granular Formant Swarm Cloud
                    renderSwarmCloud(f0, h, t, m, sigMain, sigAux);
                    break;
                case 8: // IX. Clocked Noise & Variable Resonant Dust
                    renderFilteredNoise(f0, h, t, m, sigMain, sigAux);
                    break;
                case 9: // X. Particle Friction & Bowed Exciter
                    renderParticleExciter(f0, h, t, m, sigMain, sigAux);
                    break;
                case 10: // XI. Karplus-Strong Plucked String Burst
                    renderKarplusBurst(f0, h, t, m, sigMain, sigAux);
                    break;
                case 11: // XII. Modal Mallet Strike Exciter
                default:
                    renderModalMallet(f0, h, t, m, sigMain, sigAux);
                    break;
            }

            const float lpgAmount = std::clamp(lpgEnv_ + (gateOpen ? 0.35f : 0.0f), 0.0f, 1.0f);
            const float filteredMain = svfLpg_.Process<stmlib::FILTER_MODE_LOW_PASS>(sigMain);
            const float vcaGain = 0.15f + 0.85f * lpgAmount;

            mainOut[i] = dcBlocker_.Process(stmlib::Crossfade(sigMain, filteredMain, patch.lpgColour) * vcaGain);
            auxOut[i]  = sigAux * vcaGain;
        }
    }

private:
    static inline float polyBlep(float t, float dt) noexcept {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0f;
        } else if (t > 1.0f - dt) {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    void renderVirtualAnalog(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        const float detuneRatio = stmlib::SemitonesToRatio((h - 0.5f) * 14.0f);
        const float f1 = std::min(f0 * detuneRatio, 0.48f);

        phase1_ += f0;
        if (phase1_ >= 1.0f) phase1_ -= 1.0f;
        phase2_ += f1;
        if (phase2_ >= 1.0f) phase2_ -= 1.0f;

        // Variable slope saw
        float saw1 = (2.0f * phase1_ - 1.0f) - polyBlep(phase1_, f0);
        float saw2 = (2.0f * phase2_ - 1.0f) - polyBlep(phase2_, f1);

        // Pulse width from morph
        const float pw = std::clamp(0.05f + m * 0.90f, 0.05f, 0.95f);
        float pulse = (phase1_ < pw ? 1.0f : -1.0f);
        pulse += polyBlep(phase1_, f0);
        pulse -= polyBlep(std::fmod(phase1_ + 1.0f - pw, 1.0f), f0);

        main = stmlib::Crossfade(saw1, pulse, t) * 0.6f + saw2 * 0.4f;
        aux  = (saw1 - pulse) * 0.5f;
    }

    void renderWaveshaper(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        phase1_ += f0;
        if (phase1_ >= 1.0f) phase1_ -= 1.0f;

        float sine = std::sin(phase1_ * stmlib::kTwoPi);
        float tri = 1.0f - 4.0f * std::abs(std::round(phase1_ - 0.25f) - (phase1_ - 0.25f));
        float carrier = stmlib::Crossfade(sine, tri, h);

        // Asymmetric wavefolding (Morph) + Lockhart/Serge style sine fold (Timbre)
        const float drive = 1.0f + t * 7.5f;
        const float asym = (m - 0.5f) * 1.4f;
        float x = carrier * drive + asym;
        float folded = std::sin(x * stmlib::kPi * 0.5f) - 0.35f * std::sin(x * stmlib::kPi * 1.5f);
        main = stmlib::SoftLimit(folded);
        aux  = stmlib::SoftLimit(std::tanh(x * 1.2f));
    }

    void renderTwoOpFM(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        static constexpr std::array<float, 12> kFmRatios = {
            0.5f, 1.0f, 1.4142135f, 1.5f, 2.0f, 2.756f, 3.0f, 4.0f, 5.0f, 7.0f, 9.0f, 11.31f
        };
        const float ratioPos = h * static_cast<float>(kFmRatios.size() - 2);
        const int rIdx = static_cast<int>(ratioPos);
        const float rFrac = ratioPos - static_cast<float>(rIdx);
        const float ratio = stmlib::Crossfade(kFmRatios[rIdx], kFmRatios[rIdx + 1], rFrac);

        phase1_ += f0;
        if (phase1_ >= 1.0f) phase1_ -= 1.0f;
        phase2_ += f0 * ratio;
        if (phase2_ >= 1.0f) phase2_ -= 1.0f;

        const float fb = (m - 0.5f) * 2.8f;
        const float modIndex = t * t * 10.0f * (0.3f + 0.7f * lpgEnv_);
        float modOsc = std::sin((phase2_ + prevFmSample_ * fb * 0.15f) * stmlib::kTwoPi);
        prevFmSample_ = modOsc;

        main = std::sin((phase1_ + modOsc * modIndex * 0.15f) * stmlib::kTwoPi);
        aux  = std::sin((phase1_ * 0.5f + modOsc * modIndex * 0.08f) * stmlib::kTwoPi);
    }

    void renderFormantSpeech(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        phase1_ += f0;
        if (phase1_ >= 1.0f) {
            phase1_ -= 1.0f;
            phase2_ = 0.0f;
            phase3_ = 0.0f;
        }

        // Vowel formant frequencies (F1 & F2)
        const float f1 = f0 * (1.5f + h * 5.5f);
        const float f2 = f0 * (3.5f + t * 14.0f);
        phase2_ += std::min(f1, 0.45f);
        phase3_ += std::min(f2, 0.45f);

        const float window = std::pow(1.0f - phase1_, 1.0f + m * 4.0f);
        const float s1 = std::sin(phase2_ * stmlib::kTwoPi);
        const float s2 = std::sin(phase3_ * stmlib::kTwoPi);
        main = (s1 * 0.65f + s2 * 0.45f) * window;
        aux  = (s1 * s1 - 0.5f) * window * 1.4f;
    }

    void renderHarmonicAdditive(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        phase1_ += f0;
        if (phase1_ >= 1.0f) phase1_ -= 1.0f;

        const float centerPartial = 1.0f + t * 10.0f;
        const float width = 0.8f + (1.0f - m) * 5.0f;
        const float evenOddBalance = h;

        float sumMain = 0.0f;
        float sumAux = 0.0f;
        const float baseArg = phase1_ * stmlib::kTwoPi;

        for (int k = 1; k <= 12; ++k) {
            const float fk = static_cast<float>(k);
            if (f0 * fk >= 0.47f) break;
            const float dist = (fk - centerPartial) / width;
            const float spectralEnv = std::exp(-0.5f * dist * dist) / std::sqrt(fk);
            const float parityGain = (k % 2 == 0) ? evenOddBalance : (1.0f - 0.4f * evenOddBalance);
            const float s = std::sin(baseArg * fk) * spectralEnv * parityGain;
            sumMain += s;
            if (k % 2 == 1) sumAux += s;
        }
        main = stmlib::SoftLimit(sumMain * 0.65f);
        aux  = stmlib::SoftLimit(sumAux * 0.75f);
    }

    void renderWavetable(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        phase1_ += f0;
        if (phase1_ >= 1.0f) phase1_ -= 1.0f;

        const float p = phase1_ * stmlib::kTwoPi;
        // 4 orthogonal basis waves morphed in 2D (Harmonics x Timbre) with phase distortion (Morph)
        const float pd = phase1_ + (m - 0.5f) * 0.45f * std::sin(p);
        const float w00 = std::sin(pd * stmlib::kTwoPi);
        const float w10 = std::sin(pd * stmlib::kTwoPi + std::sin(pd * stmlib::kTwoPi * 3.0f) * 0.8f);
        const float w01 = std::tanh(2.5f * std::sin(pd * stmlib::kTwoPi * 2.0f));
        const float w11 = std::sin(pd * stmlib::kTwoPi * 4.0f) * std::cos(pd * stmlib::kTwoPi);

        const float row0 = stmlib::Crossfade(w00, w10, t);
        const float row1 = stmlib::Crossfade(w01, w11, t);
        main = stmlib::Crossfade(row0, row1, h);
        aux  = row1;
    }

    void renderChordMachine(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        // 8 classic chord tables (Octave, 5th, Min7, Maj7, Min9, Maj9, Sus4, Phrygian)
        static constexpr float kChords[8][4] = {
            { 0.0f, 12.0f, 19.0f, 24.0f },
            { 0.0f,  7.0f, 12.0f, 19.0f },
            { 0.0f,  3.0f,  7.0f, 10.0f },
            { 0.0f,  4.0f,  7.0f, 11.0f },
            { 0.0f,  3.0f, 10.0f, 14.0f },
            { 0.0f,  4.0f, 11.0f, 14.0f },
            { 0.0f,  5.0f,  7.0f, 12.0f },
            { 0.0f,  1.0f,  7.0f, 12.0f }
        };

        const int cIdx = std::clamp(static_cast<int>(h * 7.99f), 0, 7);
        float sumSaw = 0.0f;
        float sumSine = 0.0f;

        for (size_t v = 0; v < 4; ++v) {
            float semis = kChords[cIdx][v];
            // Inversion morphing
            if (t > 0.5f && v == 0) semis += 12.0f;
            const float fv = std::min(f0 * stmlib::SemitonesToRatio(semis), 0.47f);
            chordPhases_[v] += fv;
            if (chordPhases_[v] >= 1.0f) chordPhases_[v] -= 1.0f;

            const float ph = chordPhases_[v];
            sumSaw  += (2.0f * ph - 1.0f) - polyBlep(ph, fv);
            sumSine += std::sin(ph * stmlib::kTwoPi);
        }

        main = stmlib::Crossfade(sumSine * 0.3f, sumSaw * 0.25f, m);
        aux  = sumSine * 0.3f;
    }

    void renderSwarmCloud(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        float sumA = 0.0f;
        float sumB = 0.0f;
        const float spread = 0.002f + h * 0.08f;

        for (size_t i = 0; i < 6; ++i) {
            const float detune = 1.0f + (static_cast<float>(i) - 2.5f) * spread;
            const float fi = std::min(f0 * detune, 0.47f);
            additivePhases_[i] += fi;
            if (additivePhases_[i] >= 1.0f) additivePhases_[i] -= 1.0f;

            const float ph = additivePhases_[i];
            const float env = 0.5f - 0.5f * std::cos(ph * stmlib::kTwoPi);
            const float formant = std::sin(ph * stmlib::kTwoPi * (1.0f + t * 5.0f));
            const float s = env * formant;
            if (i & 1) sumA += s;
            else       sumB += s;
        }
        const float noise = rng_.GetBipolar() * m * 0.25f;
        main = (sumA + sumB) * 0.28f + noise;
        aux  = (sumA - sumB) * 0.35f;
    }

    void renderFilteredNoise(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        const float clockFreq = std::clamp(f0 * (0.5f + t * 16.0f), 0.0005f, 0.48f);
        dustPhase_ += clockFreq;
        if (dustPhase_ >= 1.0f) {
            dustPhase_ -= 1.0f;
            heldNoise_ = rng_.GetBipolar();
        }
        const float white = rng_.GetBipolar();
        const float raw = stmlib::Crossfade(white, heldNoise_, h);

        svfAux_.set_f_q_fast(f0 * (1.0f + t * 8.0f), 0.8f + m * 12.0f);
        main = svfAux_.Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(raw);
        aux  = svfAux_.Process<stmlib::FILTER_MODE_LOW_PASS>(raw);
    }

    void renderParticleExciter(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        const float density = 0.0002f + t * t * 0.04f;
        float impulse = 0.0f;
        if (rng_.GetFloat() < density) {
            impulse = rng_.GetBipolar() * (1.5f + strikeEnv_);
        }
        const float continuousBow = rng_.GetGaussian() * (0.15f + m * 0.5f);
        const float freqSpread = stmlib::SemitonesToRatio(rng_.GetBipolar() * h * 18.0f);
        svfAux_.set_f_q_fast(std::max(f0 * freqSpread, 0.003f), 2.0f + m * 18.0f);

        main = svfAux_.Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(impulse + continuousBow);
        aux  = impulse * 0.6f + continuousBow * 0.4f;
    }

    void renderKarplusBurst(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        const float delaySamples = std::clamp(1.0f / std::max(f0, 0.0005f), 4.0f, static_cast<float>(ksDelayLine_.size() - 4));
        const size_t mask = ksDelayLine_.size() - 1;

        const float readPos = static_cast<float>(ksWritePtr_) - delaySamples;
        const float delayed = stmlib::InterpolateHermite(ksDelayLine_.data(), readPos + static_cast<float>(ksDelayLine_.size()), static_cast<int32_t>(mask));

        const float brightness = 0.15f + t * 0.82f;
        ONE_POLE(ksFilterState_, delayed, brightness);

        const float feedback = 0.90f + m * 0.096f;
        const float exciterNoise = rng_.GetBipolar() * strikeEnv_ * (0.4f + h * 0.6f);
        const float written = stmlib::SoftLimit(ksFilterState_ * feedback + exciterNoise);

        ksDelayLine_[ksWritePtr_ & mask] = written;
        ksWritePtr_ = (ksWritePtr_ + 1) & mask;

        main = written;
        aux  = exciterNoise;
    }

    void renderModalMallet(float f0, float h, float t, float m, float& main, float& aux) noexcept {
        // Short woody/metallic strike transient ideal for exciting Rings
        phase1_ += f0;
        if (phase1_ >= 1.0f) phase1_ -= 1.0f;
        phase2_ += f0 * (1.5f + h * 2.618f);
        if (phase2_ >= 1.0f) phase2_ -= 1.0f;

        const float malletClick = rng_.GetBipolar() * strikeEnv_ * strikeEnv_ * (0.5f + t * 0.8f);
        const float body = (std::sin(phase1_ * stmlib::kTwoPi) + 0.5f * std::sin(phase2_ * stmlib::kTwoPi)) * (strikeEnv_ + m * 0.35f);
        main = malletClick + body * 0.65f;
        aux  = malletClick;
    }

    float sampleRate_ = 48000.0f;
    float invSampleRate_ = 1.0f / 48000.0f;

    float phase1_ = 0.0f;
    float phase2_ = 0.0f;
    float phase3_ = 0.0f;
    float dustPhase_ = 0.0f;
    float heldNoise_ = 0.0f;
    float prevFmSample_ = 0.0f;

    std::array<float, 4> chordPhases_{};
    std::array<float, 6> additivePhases_{};
    std::array<float, 4096> ksDelayLine_{};
    size_t ksWritePtr_ = 0;
    float ksFilterState_ = 0.0f;

    float lpgEnv_ = 0.0f;
    float strikeEnv_ = 0.0f;
    float prevHarmonics_ = 0.5f;
    float prevTimbre_ = 0.5f;
    float prevMorph_ = 0.5f;

    stmlib::Random rng_;
    stmlib::Svf svfLpg_;
    stmlib::Svf svfAux_;
    stmlib::DcBlocker dcBlocker_;
};

} // namespace ric::dsp
