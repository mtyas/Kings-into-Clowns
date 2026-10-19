#pragma once
#include "../../Core/StmDspCore.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace ric::dsp {

struct MarblesPatch {
    bool enabled = false;
    bool tempoSync = true;    // false = Free Hz, true = DAW Tempo Sync
    int divisionIndex = 4;    // 0..9 (1/1, 1/2, 1/4, 1/8D, 1/8, 1/8T, 1/16D, 1/16, 1/16T, 1/32)
    double hostBpm = 120.0;   // Host DAW BPM
    float rateHz = 2.5f;      // [0.25..16.0] Hz (when Free)
    float jitter = 0.15f;     // [0..1] Clock randomness
    float dejaVu = 0.5f;      // [0..1] 0=fresh random, 0.5=locked loop, >0.5=permuted loop
    int loopLength = 8;       // [1..16] steps
    float spread = 0.5f;      // [0..1] Voltage distribution width
    float steps = 0.7f;       // [0..1] <0.5 smooth slewed, >=0.5 quantized scale steps
};

struct MarblesState {
    bool trigT1 = false;
    bool trigT2 = false;
    bool trigT3 = false;
    float cvX1 = 0.0f;        // [-1..+1] Quantized pitch/stepped CV
    float cvX2 = 0.0f;        // [-1..+1] Complementary CV
    float cvX3 = 0.0f;        // [-1..+1] Inverted/skewed CV
    float cvY  = 0.0f;        // [-1..+1] Smooth wandering LFO CV
    uint8_t currentStep = 0;
};

// Mutable Instruments Marbles Generative Random Sampler (T-Section & X-Section with Deja Vu Ring Buffer & DAW Sync)
class MarblesWrapper {
public:
    static constexpr size_t kLoopSize = 16;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        rng_.Init(0x4D415242u);
        reset();
    }

    void reset() noexcept {
        phase_ = 0.0f;
        periodVariation_ = 1.0f;
        stepIndex_ = 0;
        for (size_t i = 0; i < kLoopSize; ++i) {
            dejaVuT_[i]  = (i * 0.23f + 0.15f) - std::floor(i * 0.23f + 0.15f);
            dejaVuX1_[i] = std::sin(static_cast<float>(i) * 0.85f) * 0.65f;
            dejaVuX2_[i] = std::cos(static_cast<float>(i) * 1.15f) * 0.65f;
            dejaVuX3_[i] = std::sin(static_cast<float>(i) * 0.45f + 1.0f) * 0.65f;
        }
        slewX1_ = 0.0f;
        slewX2_ = 0.0f;
        slewX3_ = 0.0f;
        yPhase_ = 0.0f;
        yPrev_ = 0.0f;
        yTarget_ = 0.0f;
        yCurrent_ = 0.0f;
        targetX1_ = targetX2_ = targetX3_ = 0.0f;
    }

    static float divisionToHz(double bpm, int divIndex) noexcept {
        // Multipliers in steps-per-quarter-note:
        // 0: 1/1 Bar (0.25), 1: 1/2 (0.5), 2: 1/4 (1.0), 3: 1/8D (1.3333), 4: 1/8 (2.0),
        // 5: 1/8T (3.0), 6: 1/16D (2.6667), 7: 1/16 (4.0), 8: 1/16T (6.0), 9: 1/32 (8.0)
        static constexpr std::array<float, 10> kMults = {
            0.25f, 0.5f, 1.0f, 1.3333333f, 2.0f, 3.0f, 2.6666667f, 4.0f, 6.0f, 8.0f
        };
        const float quarterHz = static_cast<float>(std::clamp(bpm, 30.0, 300.0) / 60.0);
        const int idx = std::clamp(divIndex, 0, 9);
        return quarterHz * kMults[static_cast<size_t>(idx)];
    }

    // Advances the generative clock by `numSamples` and returns the current sub-block triggers & CVs
    MarblesState stepSubBlock(const MarblesPatch& patch, size_t numSamples) noexcept {
        MarblesState out{};
        if (!patch.enabled) {
            out.currentStep = static_cast<uint8_t>(stepIndex_);
            return out;
        }

        const float dt = static_cast<float>(numSamples) / sampleRate_;
        const float baseClockHz = patch.tempoSync
            ? divisionToHz(patch.hostBpm, patch.divisionIndex)
            : std::clamp(patch.rateHz, 0.1f, 24.0f);
        const float effectiveRate = baseClockHz * periodVariation_;
        phase_ += effectiveRate * dt;

        // Smooth wandering Y generator (1/4th clock rate)
        yPhase_ += effectiveRate * 0.25f * dt;
        if (yPhase_ >= 1.0f) {
            yPhase_ -= 1.0f;
            yPrev_ = yTarget_;
            yTarget_ = rng_.GetBipolar();
        }
        const float ySmooth = 0.5f - 0.5f * std::cos(yPhase_ * stmlib::kPi);
        yCurrent_ = stmlib::Crossfade(yPrev_, yTarget_, ySmooth);

        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            const int len = std::clamp(patch.loopLength, 1, 16);
            stepIndex_ = (stepIndex_ + 1) % static_cast<size_t>(len);

            // Evaluate Deja-Vu probability:
            // At dejaVu = 0.5 -> 100% locked loop. At 0.0 -> 100% new random. At 1.0 -> random step jump inside loop.
            const float dv = std::clamp(patch.dejaVu, 0.0f, 1.0f);
            if (dv > 0.65f && rng_.GetFloat() < (dv - 0.65f) * 2.5f) {
                stepIndex_ = rng_.GetWord() % static_cast<uint32_t>(len);
            }

            const float lockProb = 1.0f - std::abs(dv - 0.5f) * 2.0f;
            if (rng_.GetFloat() > lockProb) {
                dejaVuT_[stepIndex_]  = rng_.GetFloat();
                dejaVuX1_[stepIndex_] = sampleDistribution(patch.spread);
                dejaVuX2_[stepIndex_] = sampleDistribution(patch.spread);
                dejaVuX3_[stepIndex_] = sampleDistribution(patch.spread);
            }

            // Clock period jitter (Marbles T-generator jitter)
            const float jit = std::clamp(patch.jitter, 0.0f, 1.0f);
            periodVariation_ = std::clamp(1.0f + rng_.GetGaussian() * jit * 0.55f, 0.35f, 2.2f);

            // T1 / T2 / T3 Bernoulli gate routing
            const float u = dejaVuT_[stepIndex_];
            out.trigT2 = true; // Master clock pulse
            if (u < 0.52f) {
                out.trigT1 = true;
            } else {
                out.trigT3 = true;
            }

            targetX1_ = quantizeVoltage(dejaVuX1_[stepIndex_], patch.steps);
            targetX2_ = quantizeVoltage(dejaVuX2_[stepIndex_], patch.steps);
            targetX3_ = quantizeVoltage(dejaVuX3_[stepIndex_], patch.steps);
        }

        // Smooth vs Stepped interpolation (Marbles STEPS knob)
        const float slewCoeff = (patch.steps < 0.45f)
            ? (0.02f + patch.steps * 0.4f)
            : 0.85f;

        ONE_POLE(slewX1_, targetX1_, slewCoeff);
        ONE_POLE(slewX2_, targetX2_, slewCoeff);
        ONE_POLE(slewX3_, targetX3_, slewCoeff);

        out.cvX1 = slewX1_;
        out.cvX2 = slewX2_;
        out.cvX3 = slewX3_;
        out.cvY  = yCurrent_;
        out.currentStep = static_cast<uint8_t>(stepIndex_);
        return out;
    }

    const std::array<float, kLoopSize>& getLoopX1() const noexcept { return dejaVuX1_; }

    // Quantizes a normalized [-1..1] CV to a musical scale offset in semitones
    static float quantizeToScaleSemitones(float bipolarCv, float spreadSemitones = 14.0f) noexcept {
        static constexpr std::array<float, 7> kScale = { 0.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 12.0f };
        const float rawSemis = bipolarCv * spreadSemitones;
        const float octaves = std::floor(rawSemis / 12.0f);
        const float rem = rawSemis - octaves * 12.0f;

        float best = 0.0f;
        float minDiff = 99.0f;
        for (float deg : kScale) {
            float d = std::abs(rem - deg);
            if (d < minDiff) {
                minDiff = d;
                best = deg;
            }
        }
        return octaves * 12.0f + best;
    }

private:
    float sampleDistribution(float spread) noexcept {
        const float uni = rng_.GetBipolar();
        const float gauss = rng_.GetGaussian();
        const float bimodal = (uni >= 0.0f ? 1.0f : -1.0f) * std::pow(std::abs(uni), 0.35f);
        if (spread < 0.5f) {
            return gauss * (spread * 2.0f);
        }
        return stmlib::Crossfade(uni, bimodal, (spread - 0.5f) * 2.0f);
    }

    static float quantizeVoltage(float v, float steps) noexcept {
        if (steps <= 0.5f) return v;
        const float numSteps = 2.0f + (1.0f - steps) * 20.0f;
        return std::round(v * numSteps) / numSteps;
    }

    float sampleRate_ = 48000.0f;
    float phase_ = 0.0f;
    float periodVariation_ = 1.0f;
    size_t stepIndex_ = 0;

    std::array<float, kLoopSize> dejaVuT_{};
    std::array<float, kLoopSize> dejaVuX1_{};
    std::array<float, kLoopSize> dejaVuX2_{};
    std::array<float, kLoopSize> dejaVuX3_{};

    float targetX1_ = 0.0f;
    float targetX2_ = 0.0f;
    float targetX3_ = 0.0f;
    float slewX1_ = 0.0f;
    float slewX2_ = 0.0f;
    float slewX3_ = 0.0f;

    float yPhase_ = 0.0f;
    float yPrev_ = 0.0f;
    float yTarget_ = 0.0f;
    float yCurrent_ = 0.0f;

    stmlib::Random rng_;
};

} // namespace ric::dsp
