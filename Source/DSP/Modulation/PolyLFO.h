#pragma once
#include "../../Core/StmDspCore.h"
#include <cmath>
#include <algorithm>

namespace ric::mod {

class PolyLFO {
public:
    void prepare(double sampleRate, float initialPhase = 0.0f) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        phase_ = std::fmod(initialPhase, 1.0f);
        rng_.Init(static_cast<uint32_t>(initialPhase * 100000.0f) + 0x918273u);
    }

    void reset(float phaseOffset = 0.0f) noexcept {
        phase_ = std::fmod(phaseOffset, 1.0f);
    }

    // Free-running: steps by `numSamples` and returns a bipolar LFO in [-1, +1]
    // shape: 0=Sine, 1=Triangle, 2=Ramp Saw, 3=Sample & Hold
    float stepSubBlock(float rateHz, size_t numSamples, int shape = 0) noexcept {
        const float dt = static_cast<float>(numSamples) / sampleRate_;
        phase_ += std::clamp(rateHz, 0.01f, 30.0f) * dt;
        bool wrapped = false;
        if (phase_ >= 1.0f) {
            phase_ -= std::floor(phase_);
            wrapped = true;
        }
        return evaluate(shape, wrapped);
    }

    // Tempo-locked: jumps straight to the phase the host transport dictates
    float stepToPhase(float newPhase, int shape) noexcept {
        newPhase -= std::floor(newPhase);
        const bool wrapped = newPhase < phase_;
        phase_ = newPhase;
        return evaluate(shape, wrapped);
    }

    float getValue() const noexcept { return value_; }

private:
    float evaluate(int shape, bool wrapped) noexcept {
        switch (shape) {
            case 0: // Sine
                value_ = std::sin(phase_ * stmlib::kTwoPi);
                break;
            case 1: // Triangle
                value_ = 1.0f - 4.0f * std::abs(std::round(phase_ - 0.25f) - (phase_ - 0.25f));
                break;
            case 2: // Ramp Saw
                value_ = 2.0f * phase_ - 1.0f;
                break;
            case 3: // Smooth Sample & Hold
            default:
                if (wrapped) shTarget_ = rng_.GetBipolar();
                ONE_POLE(value_, shTarget_, 0.35f);
                break;
        }
        return value_;
    }

    float sampleRate_ = 48000.0f;
    float phase_ = 0.0f;
    float value_ = 0.0f;
    float shTarget_ = 0.0f;
    stmlib::Random rng_;
};

} // namespace ric::mod
