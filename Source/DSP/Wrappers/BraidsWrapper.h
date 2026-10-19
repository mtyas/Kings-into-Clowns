#pragma once
#include "../../Core/StmDspCore.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace ric::dsp {

// Secondary Macro Oscillator based on Mutable Instruments Braids
// Provides 16-bit integer-quantized character, CSAW, ZLPF, Vosim, Triple Ring-Mod, and Toy/Bitcrush
class BraidsWrapper {
public:
    static constexpr size_t kMaxBlockSize = 24;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        invSampleRate_ = 1.0f / sampleRate_;
        reset();
    }

    void reset() noexcept {
        phaseA_ = 0.0f;
        phaseB_ = 0.0f;
        phaseC_ = 0.0f;
    }

    void renderSubBlock(int shapeIndex, float midiNote, float timbre, float color, float* out, size_t size) noexcept {
        const float freq = std::clamp(stmlib::MidiToFrequency(midiNote), 16.0f, sampleRate_ * 0.45f);
        const float f0 = freq * invSampleRate_;

        const float t = std::clamp(timbre, 0.0f, 1.0f);
        const float c = std::clamp(color, 0.0f, 1.0f);

        for (size_t i = 0; i < size; ++i) {
            phaseA_ += f0;
            if (phaseA_ >= 1.0f) phaseA_ -= 1.0f;

            const float ratioB = 1.0f + c * 3.0f;
            const float ratioC = 0.5f + t * 2.5f;
            phaseB_ += std::min(f0 * ratioB, 0.47f);
            if (phaseB_ >= 1.0f) phaseB_ -= 1.0f;
            phaseC_ += std::min(f0 * ratioC, 0.47f);
            if (phaseC_ >= 1.0f) phaseC_ -= 1.0f;

            float sample = 0.0f;
            switch (shapeIndex % 4) {
                case 0: { // CSAW (Braids signature cosine-notched sawtooth)
                    float saw = 2.0f * phaseA_ - 1.0f;
                    float notch = (phaseA_ < 0.15f + t * 0.35f) ? -std::cos(phaseA_ * stmlib::kTwoPi * 3.0f) : saw;
                    sample = stmlib::Crossfade(saw, notch, 0.5f + 0.45f * c);
                    break;
                }
                case 1: { // ZLPF / Peak-Resonant Sync Wave
                    float syncPhase = std::fmod(phaseA_ * (1.0f + t * 6.0f), 1.0f);
                    float win = 1.0f - phaseA_;
                    sample = std::sin(syncPhase * stmlib::kTwoPi) * win * (1.0f + c);
                    break;
                }
                case 2: { // Triple Ring-Mod & Fold
                    float sA = std::sin(phaseA_ * stmlib::kTwoPi);
                    float sB = std::sin(phaseB_ * stmlib::kTwoPi);
                    float sC = std::cos(phaseC_ * stmlib::kTwoPi);
                    sample = sA * (1.0f - t) + (sA * sB) * t * (1.0f - c) + (sA * sB * sC) * t * c;
                    break;
                }
                case 3:
                default: { // Braids 12-bit DAC / Bit-quantized Harmonic Fold
                    float s = std::sin(phaseA_ * stmlib::kTwoPi + std::sin(phaseB_ * stmlib::kTwoPi) * t * 2.5f);
                    int16_t dac12 = stmlib::Clip16(static_cast<int32_t>(s * 32767.0f));
                    int16_t mask = static_cast<int16_t>(0xFFF0u << static_cast<int>((1.0f - c) * 4.0f));
                    sample = static_cast<float>(dac12 & mask) * (1.0f / 32768.0f);
                    break;
                }
            }

            // Emulate Braids 16-bit DAC saturation warmth
            int16_t q = stmlib::Clip16(static_cast<int32_t>(sample * 28000.0f));
            out[i] = static_cast<float>(q) * (1.0f / 32768.0f);
        }
    }

private:
    float sampleRate_ = 48000.0f;
    float invSampleRate_ = 1.0f / 48000.0f;
    float phaseA_ = 0.0f;
    float phaseB_ = 0.0f;
    float phaseC_ = 0.0f;
};

} // namespace ric::dsp
