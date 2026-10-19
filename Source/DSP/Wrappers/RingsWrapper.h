#pragma once
#include "../../Core/StmDspCore.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ric::dsp {

struct RingsPatch {
    int model = 0;            // 0..5 (Modal, Sympathetic, Inharmonic String, FM, Sympathetic Quant, String+Reverb)
    int polyphony = 1;        // 1, 2, or 4 resonators inside this voice
    float frequency = 60.0f;  // MIDI note
    float structure = 0.5f;   // [0..1] Inharmonicity / Chord interval / Dispersion
    float brightness = 0.5f;  // [0..1] High-frequency modal amplitude / low-pass damping
    float damping = 0.7f;     // [0..1] Decay time (T60)
    float position = 0.35f;   // [0..1] Excitation pickup/strike point
    float internalExciter = 0.5f; // [0..1] Internal Pluck/Mallet/Bow burst level
};

// Mutable Instruments Rings Physical Modeling Resonator
// Implements 24-mode SVF Modal Bank, Sympathetic String Network, Nonlinear Dispersion String,
// FM Follower Voice, Sympathetic Quantized Chords, and Cavern String Reverb.
class RingsWrapper {
public:
    static constexpr size_t kMaxBlockSize = 24;
    static constexpr size_t kNumModalModes = 24;
    static constexpr size_t kMaxStrings = 4;
    static constexpr size_t kStringDelaySize = 8192;
    static constexpr size_t kStringDelayMask = kStringDelaySize - 1;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        invSampleRate_ = 1.0f / sampleRate_;
        rng_.Init(0x9E3779B9u);

        for (auto& svf : modalFilters_) {
            svf.Init();
        }
        for (size_t s = 0; s < kMaxStrings; ++s) {
            stringDelays_[s].assign(kStringDelaySize, 0.0f);
            dispersionSvf_[s].Init();
            dcBlockers_[s].Init(0.9994f);
        }
        exciterFilter_.Init();
        reverb_.Init();
        reset();
    }

    void reset() noexcept {
        for (auto& svf : modalFilters_) svf.Reset();
        for (size_t s = 0; s < kMaxStrings; ++s) {
            std::fill(stringDelays_[s].begin(), stringDelays_[s].end(), 0.0f);
            stringWritePtr_[s] = 0;
            stringLpState_[s] = 0.0f;
            dispersionSvf_[s].Reset();
            dcBlockers_[s].Reset();
        }
        exciterFilter_.Reset();
        reverb_.Clear();
        strumMalletEnv_ = 0.0f;
        strumNoiseEnv_ = 0.0f;
        fmPhaseC_ = 0.0f;
        fmPhaseM_ = 0.0f;
        fmFeedback_ = 0.0f;
        fmEnvFollower_ = 0.0f;
        rmsEnergy_ = 0.0f;
    }

    void triggerStrum(float velocity = 1.0f) noexcept {
        const float vel = std::clamp(velocity, 0.05f, 1.0f);
        strumMalletEnv_ = vel;
        strumNoiseEnv_ = vel;

        // Pre-seed sympathetic strings with micro-pluck impulse
        for (size_t s = 0; s < kMaxStrings; ++s) {
            const size_t ptr = stringWritePtr_[s];
            for (size_t i = 0; i < 32; ++i) {
                float window = std::sin(static_cast<float>(i) * (stmlib::kPi / 32.0f));
                stringDelays_[s][(ptr + i) & kStringDelayMask] += rng_.GetBipolar() * vel * window * 0.35f;
            }
        }
    }

    float getRmsEnergy() const noexcept { return rmsEnergy_; }

    // Renders a 24-sample sub-block into Odd and Even resonator outputs
    void processSubBlock(const RingsPatch& patch,
                         const float* externalExcitation,
                         float* outOdd,
                         float* outEven,
                         size_t size) noexcept {
        const float baseFreq = std::clamp(stmlib::MidiToFrequency(patch.frequency), 20.0f, sampleRate_ * 0.42f);
        const float f0 = baseFreq * invSampleRate_;

        const float structVal = std::clamp(patch.structure, 0.0f, 0.995f);
        const float brightVal = std::clamp(patch.brightness, 0.0f, 1.0f);
        const float dampVal   = std::clamp(patch.damping, 0.0f, 0.998f);
        const float posVal    = std::clamp(patch.position, 0.02f, 0.98f);

        // Configure internal mallet/noise burst exciter (Rings Strummer + Internal Exciter)
        const float malletCutoff = std::clamp(f0 * (2.0f + brightVal * 18.0f), 0.002f, 0.45f);
        exciterFilter_.set_f_q(malletCutoff, 1.2f + brightVal * 2.5f);
        const float malletDecay = std::exp(-1.0f / (0.004f * sampleRate_));
        const float burstDecay  = std::exp(-1.0f / ((0.012f + dampVal * 0.04f) * sampleRate_));

        // The modal bank's normalized band-passes barely ring from a 4 ms strike, so the internal
        // mallet hits it harder (external / tonal excitation is left as is).
        const float strikeGain = (patch.model == 0) ? 60.0f : 1.0f;

        float subBlockExcitation[kMaxBlockSize]{};
        for (size_t i = 0; i < size; ++i) {
            float mallet = strumMalletEnv_ * (1.0f - 2.0f * (strumMalletEnv_ < 0.5f ? 1.0f : 0.0f));
            strumMalletEnv_ *= malletDecay;

            float noiseBurst = rng_.GetBipolar() * strumNoiseEnv_ * 0.6f;
            strumNoiseEnv_ *= burstDecay;

            float internalEx = exciterFilter_.Process<stmlib::FILTER_MODE_LOW_PASS>(mallet * 0.8f + noiseBurst) * patch.internalExciter * strikeGain;
            subBlockExcitation[i] = externalExcitation[i] + internalEx;
        }

        switch (patch.model) {
            case 0: // I. Modal Resonator (24-Mode Plate / Bell / Mallet Bank)
                processModalResonator(f0, structVal, brightVal, dampVal, posVal, subBlockExcitation, outOdd, outEven, size);
                break;
            case 1: // II. Sympathetic Strings (Coupled Bridge Network)
                processSympatheticStrings(f0, structVal, brightVal, dampVal, posVal, patch.polyphony, false, subBlockExcitation, outOdd, outEven, size);
                break;
            case 2: // III. Inharmonic / Nonlinear Dispersion String
                processInharmonicStrings(f0, structVal, brightVal, dampVal, posVal, patch.polyphony, subBlockExcitation, outOdd, outEven, size);
                break;
            case 3: // IV. 2-Op FM Voice with Envelope Follower
                processFmResonator(f0, structVal, brightVal, dampVal, posVal, subBlockExcitation, outOdd, outEven, size);
                break;
            case 4: // V. Sympathetic Quantized Chords (Easter-Egg Chords + Strings)
                processSympatheticStrings(f0, structVal, brightVal, dampVal, posVal, 4, true, subBlockExcitation, outOdd, outEven, size);
                break;
            case 5: // VI. String & Western Cavern Reverb
            default:
                processInharmonicStrings(f0, structVal, brightVal, dampVal, posVal, patch.polyphony, subBlockExcitation, outOdd, outEven, size);
                reverb_.Process(outOdd, outEven, size, 0.35f + posVal * 0.5f, 0.65f + dampVal * 0.3f, 0.68f, 0.4f + brightVal * 0.5f, 0.35f);
                break;
        }

        // Update RMS energy & apply gentle output limiting
        float energyAccum = 0.0f;
        for (size_t i = 0; i < size; ++i) {
            outOdd[i]  = stmlib::SoftLimit(outOdd[i]);
            outEven[i] = stmlib::SoftLimit(outEven[i]);
            energyAccum += outOdd[i] * outOdd[i] + outEven[i] * outEven[i];
        }
        ONE_POLE(rmsEnergy_, std::sqrt(energyAccum / static_cast<float>(size * 2)), 0.15f);
    }

private:
    // I. Modal Resonator: 24 Bandpass SVFs with structure-controlled stiff-string/plate dispersion
    void processModalResonator(float f0, float structure, float brightness, float damping, float position,
                               const float* in, float* outOdd, float* outEven, size_t size) noexcept {
        // Stiff-bar / plate dispersion factor B from structure
        // structure = 0.25 -> pure harmonic ratios; higher -> bell/plate partials
        const float stiffB = (structure - 0.24f) * std::abs(structure - 0.24f) * 0.38f;
        const float baseQ = 35.0f * std::pow(220.0f, damping);
        const float brightRollOff = 1.8f * (1.0f - brightness);

        std::array<float, kNumModalModes> modeGains{};
        for (size_t m = 0; m < kNumModalModes; ++m) {
            const float k = static_cast<float>(m + 1);
            // Inharmonic partial ratio: k * sqrt(1 + B * k^2) + subtle subharmonic/bell detune
            const float detune = (m % 3 == 1) ? (structure * 0.04f) : 0.0f;
            const float ratio = k * std::sqrt(std::max(0.05f, 1.0f + stiffB * k * k)) + detune;
            const float fk = f0 * ratio;

            if (fk >= 0.46f) {
                modeGains[m] = 0.0f;
                continue;
            }

            // High-frequency damping roll-off
            const float hfLoss = std::pow(k, -brightRollOff);
            const float modeQ = std::clamp(baseQ * hfLoss, 5.0f, 14000.0f);
            modalFilters_[m].set_f_q(fk, modeQ);

            // Spatial strike position comb profile: sin(k * pi * position), Q-compensated for musical bell/plate energy
            const float posWeight = std::sin(k * stmlib::kPi * position);
            modeGains[m] = posWeight * hfLoss * std::sqrt(modeQ) * 0.14f / std::sqrt(k);
        }

        for (size_t i = 0; i < size; ++i) {
            const float ex = in[i] * 0.35f;
            float oddSum = 0.0f;
            float evenSum = 0.0f;

            for (size_t m = 0; m < kNumModalModes; ++m) {
                if (modeGains[m] == 0.0f) continue;
                const float modeOut = modalFilters_[m].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(ex) * modeGains[m];
                if ((m & 1) == 0) oddSum  += modeOut;
                else              evenSum += modeOut;
            }

            outOdd[i]  = oddSum * 1.35f;
            outEven[i] = evenSum * 1.35f;
        }
    }

    // II & V. Sympathetic Strings: 4 coupled waveguide strings sharing a resonant bridge
    void processSympatheticStrings(float f0, float structure, float brightness, float damping, float position,
                                   int polySelect, bool quantizedChords,
                                   const float* in, float* outOdd, float* outEven, size_t size) noexcept {
        // Rings sympathetic interval tables (semitones)
        static constexpr float kSympatheticIntervals[9][4] = {
            { 0.0f, -12.0f, -12.0f,   0.0f },
            { 0.0f, -12.0f,  -5.0f,   0.0f },
            { 0.0f, -12.0f,   7.0f,  12.0f },
            { 0.0f,   0.01f,  7.0f,  12.0f },
            { 0.0f,   3.0f,   7.0f,  10.0f },
            { 0.0f,   4.0f,   7.0f,  11.0f },
            { 0.0f,   5.0f,   7.0f,  12.0f },
            { 0.0f,   7.0f,  12.0f,  19.0f },
            { 0.0f,  12.0f,  19.0f,  24.0f }
        };

        // Sympathetic networks need at least two strings to couple
        const size_t numStrings = quantizedChords ? 4 : static_cast<size_t>(std::clamp(polySelect + 2, 2, 4));
        const float tablePos = structure * 8.0f;
        const int idxA = std::clamp(static_cast<int>(tablePos), 0, 7);
        const float frac = quantizedChords ? 0.0f : (tablePos - static_cast<float>(idxA));

        std::array<float, kMaxStrings> delays{};
        std::array<float, kMaxStrings> gains{};
        const float t60Sec = 0.08f * std::pow(140.0f, damping);
        const float lpCoeff = 0.08f + brightness * 0.88f;

        for (size_t s = 0; s < numStrings; ++s) {
            const float semi = stmlib::Crossfade(kSympatheticIntervals[idxA][s], kSympatheticIntervals[idxA + 1][s], frac);
            const float fs = std::clamp(f0 * stmlib::SemitonesToRatio(semi), 0.0004f, 0.45f);
            delays[s] = std::clamp((1.0f / fs) - 0.5f, 4.0f, static_cast<float>(kStringDelaySize - 8));
            // Per-period feedback gain for target T60
            gains[s] = std::pow(0.001f, 1.0f / (fs * sampleRate_ * t60Sec));
        }

        const int combOffset = std::clamp(static_cast<int>(delays[0] * position * 0.5f), 1, 512);

        for (size_t i = 0; i < size; ++i) {
            const float ex = in[i] * 0.5f;
            float bridgeCoupling = 0.0f;
            std::array<float, kMaxStrings> stringOuts{};

            for (size_t s = 0; s < numStrings; ++s) {
                const float readIdx = static_cast<float>(stringWritePtr_[s]) - delays[s] + static_cast<float>(kStringDelaySize);
                const float rawDelayed = stmlib::InterpolateHermite(stringDelays_[s].data(), readIdx, static_cast<int32_t>(kStringDelayMask));
                stringOuts[s] = rawDelayed;
                bridgeCoupling += rawDelayed;
            }
            bridgeCoupling *= (0.045f / static_cast<float>(numStrings));

            float oddSum = 0.0f;
            float evenSum = 0.0f;

            for (size_t s = 0; s < numStrings; ++s) {
                ONE_POLE(stringLpState_[s], stringOuts[s], lpCoeff);
                const float stringEx = (s == 0) ? ex : (ex * 0.35f);
                const float nextVal = dcBlockers_[s].Process(stringLpState_[s] * gains[s] + stringEx + bridgeCoupling);

                const size_t wPtr = stringWritePtr_[s] & kStringDelayMask;
                stringDelays_[s][wPtr] = stmlib::SoftLimit(nextVal);

                // Comb pickup position filter
                const float pickupTap = stringDelays_[s][(wPtr - combOffset) & kStringDelayMask];
                const float voiced = stringOuts[s] - pickupTap * 0.65f;

                stringWritePtr_[s] = (wPtr + 1) & kStringDelayMask;

                if ((s & 1) == 0) oddSum  += voiced;
                else              evenSum += voiced;
            }

            outOdd[i]  = oddSum * 0.85f;
            outEven[i] = (numStrings > 1 ? evenSum : oddSum) * 0.85f;
        }
    }

    // III & VI. Inharmonic String with All-Pass Dispersion & Bridge Absorption
    void processInharmonicStrings(float f0, float structure, float brightness, float damping, float position,
                                  int polySelect, const float* in, float* outOdd, float* outEven, size_t size) noexcept {
        const size_t numStrings = (polySelect == 0) ? 1 : (polySelect == 1 ? 2 : 4);
        const float t60Sec = 0.08f * std::pow(140.0f, damping);
        const float lpCoeff = 0.08f + brightness * 0.88f;
        const float dispersionAmount = (structure - 0.5f) * 1.6f;

        std::array<float, kMaxStrings> delays{};
        std::array<float, kMaxStrings> gains{};

        for (size_t s = 0; s < numStrings; ++s) {
            const float detuneSemi = static_cast<float>(s) * (structure * 7.0f);
            const float fs = std::clamp(f0 * stmlib::SemitonesToRatio(detuneSemi), 0.0004f, 0.45f);
            delays[s] = std::clamp((1.0f / fs) - 1.0f, 4.0f, static_cast<float>(kStringDelaySize - 8));
            gains[s] = std::pow(0.001f, 1.0f / (fs * sampleRate_ * t60Sec));
            dispersionSvf_[s].set_f_q(std::clamp(fs * (2.5f + std::abs(dispersionAmount) * 6.0f), 0.002f, 0.44f), 0.9f);
        }

        const int combOffset = std::clamp(static_cast<int>(delays[0] * position * 0.5f), 1, 512);

        for (size_t i = 0; i < size; ++i) {
            const float ex = in[i] * 0.6f;
            float oddSum = 0.0f;
            float evenSum = 0.0f;

            for (size_t s = 0; s < numStrings; ++s) {
                // Nonlinear pitch modulation by string amplitude when structure > 0.6
                const float nonLinPitch = (structure > 0.55f) ? (stringLpState_[s] * (structure - 0.55f) * 6.0f) : 0.0f;
                const float totalDelay = std::clamp(delays[s] + nonLinPitch, 4.0f, static_cast<float>(kStringDelaySize - 8));

                const float readIdx = static_cast<float>(stringWritePtr_[s]) - totalDelay + static_cast<float>(kStringDelaySize);
                const float delayed = stmlib::InterpolateHermite(stringDelays_[s].data(), readIdx, static_cast<int32_t>(kStringDelayMask));

                // All-pass dispersion approximation using SVF bandpass phase shift
                const float bp = dispersionSvf_[s].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(delayed);
                const float dispersed = delayed - bp * dispersionAmount;

                ONE_POLE(stringLpState_[s], dispersed, lpCoeff);
                const float nextVal = dcBlockers_[s].Process(stringLpState_[s] * gains[s] + ex);

                const size_t wPtr = stringWritePtr_[s] & kStringDelayMask;
                stringDelays_[s][wPtr] = stmlib::SoftLimit(nextVal);

                const float pickupTap = stringDelays_[s][(wPtr - combOffset) & kStringDelayMask];
                const float voiced = dispersed - pickupTap * 0.6f;

                stringWritePtr_[s] = (wPtr + 1) & kStringDelayMask;

                if ((s & 1) == 0) oddSum  += voiced;
                else              evenSum += voiced;
            }

            if (numStrings == 1) {
                // Stereoize single string via odd/even comb taps
                const size_t wPtr = (stringWritePtr_[0] - 1) & kStringDelayMask;
                const float tapR = stringDelays_[0][(wPtr - combOffset / 2 - 1) & kStringDelayMask];
                outOdd[i]  = oddSum;
                outEven[i] = oddSum * 0.7f + tapR * 0.5f;
            } else {
                outOdd[i]  = oddSum * 0.85f;
                outEven[i] = evenSum * 0.85f;
            }
        }
    }

    // IV. 2-Op FM Voice with Input Envelope Follower (Rings FM Mode)
    void processFmResonator(float f0, float structure, float brightness, float damping, float position,
                            const float* in, float* outOdd, float* outEven, size_t size) noexcept {
        static constexpr std::array<float, 10> kRatios = {
            0.25f, 0.5f, 1.0f, 1.4142f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f
        };
        const float rPos = structure * static_cast<float>(kRatios.size() - 2);
        const int rIdx = static_cast<int>(rPos);
        const float ratio = stmlib::Crossfade(kRatios[rIdx], kRatios[rIdx + 1], rPos - static_cast<float>(rIdx));

        const float envDecay = std::exp(-1.0f / ((0.02f + std::pow(damping, 3.0f) * 4.5f) * sampleRate_));
        const float fbAmount = (position - 0.5f) * 2.5f;

        for (size_t i = 0; i < size; ++i) {
            const float absIn = std::abs(in[i]);
            if (absIn > fmEnvFollower_) {
                ONE_POLE(fmEnvFollower_, absIn, 0.25f);
            } else {
                fmEnvFollower_ *= envDecay;
            }

            fmPhaseC_ += f0;
            if (fmPhaseC_ >= 1.0f) fmPhaseC_ -= 1.0f;
            fmPhaseM_ += f0 * ratio;
            if (fmPhaseM_ >= 1.0f) fmPhaseM_ -= 1.0f;

            const float modIndex = (brightness * 6.0f) * (0.15f + 0.85f * fmEnvFollower_);
            const float modSample = std::sin((fmPhaseM_ + fmFeedback_ * fbAmount * 0.15f) * stmlib::kTwoPi);
            fmFeedback_ = modSample;

            const float carrier = std::sin((fmPhaseC_ + modSample * modIndex * 0.16f + in[i] * 0.2f) * stmlib::kTwoPi);
            const float subCarrier = std::cos((fmPhaseC_ + modSample * modIndex * 0.08f) * stmlib::kTwoPi);

            outOdd[i]  = carrier * fmEnvFollower_ * 1.4f;
            outEven[i] = subCarrier * fmEnvFollower_ * 1.4f;
        }
    }

    float sampleRate_ = 48000.0f;
    float invSampleRate_ = 1.0f / 48000.0f;

    std::array<stmlib::Svf, kNumModalModes> modalFilters_{};
    std::array<std::vector<float>, kMaxStrings> stringDelays_{};
    std::array<size_t, kMaxStrings> stringWritePtr_{};
    std::array<float, kMaxStrings> stringLpState_{};
    std::array<stmlib::Svf, kMaxStrings> dispersionSvf_{};
    std::array<stmlib::DcBlocker, kMaxStrings> dcBlockers_{};

    stmlib::Svf exciterFilter_;
    stmlib::DiffusionReverb reverb_;
    stmlib::Random rng_;

    float strumMalletEnv_ = 0.0f;
    float strumNoiseEnv_ = 0.0f;
    float fmPhaseC_ = 0.0f;
    float fmPhaseM_ = 0.0f;
    float fmFeedback_ = 0.0f;
    float fmEnvFollower_ = 0.0f;
    float rmsEnergy_ = 0.0f;
};

} // namespace ric::dsp
