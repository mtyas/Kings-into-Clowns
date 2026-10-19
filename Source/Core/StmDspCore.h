#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

namespace stmlib {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

// Portable replacements for STM32 intrinsics (__SSAT / __USAT)
inline int16_t Clip16(int32_t x) noexcept {
    return static_cast<int16_t>(std::clamp<int32_t>(x, -32768, 32767));
}

inline uint16_t ClipU16(int32_t x) noexcept {
    return static_cast<uint16_t>(std::clamp<int32_t>(x, 0, 65535));
}

inline float SoftLimit(float x) noexcept {
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float SoftClip(float x) noexcept {
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    return SoftLimit(x);
}

inline float Crossfade(float a, float b, float fade) noexcept {
    return a + (b - a) * fade;
}

inline float Interpolate(const float* table, float index, float size) noexcept {
    index *= size;
    auto integral = static_cast<int32_t>(index);
    float fractional = index - static_cast<float>(integral);
    return table[integral] + (table[integral + 1] - table[integral]) * fractional;
}

inline float InterpolateHermite(const float* table, float index, int32_t sizeMask) noexcept {
    auto integral = static_cast<int32_t>(std::floor(index));
    float f = index - static_cast<float>(integral);
    float xm1 = table[(integral - 1) & sizeMask];
    float x0  = table[(integral)     & sizeMask];
    float x1  = table[(integral + 1) & sizeMask];
    float x2  = table[(integral + 2) & sizeMask];
    float c = (x1 - xm1) * 0.5f;
    float v = x0 - x1;
    float w = c + v;
    float a = w + v + (x2 - x0) * 0.5f;
    float b_neg = w + a;
    return ((((a * f) - b_neg) * f + c) * f + x0);
}

// Hermite read with a double-precision position, so long circular buffers keep sub-sample accuracy
inline float InterpolateHermite(const float* table, double index, int32_t sizeMask) noexcept {
    const double fl = std::floor(index);
    const auto integral = static_cast<int32_t>(static_cast<int64_t>(fl) & sizeMask);
    const auto f = static_cast<float>(index - fl);
    float xm1 = table[(integral - 1) & sizeMask];
    float x0  = table[(integral)     & sizeMask];
    float x1  = table[(integral + 1) & sizeMask];
    float x2  = table[(integral + 2) & sizeMask];
    float c = (x1 - xm1) * 0.5f;
    float v = x0 - x1;
    float w = c + v;
    float a = w + v + (x2 - x0) * 0.5f;
    float b_neg = w + a;
    return ((((a * f) - b_neg) * f + c) * f + x0);
}

inline float SemitonesToRatio(float semitones) noexcept {
    return std::pow(2.0f, semitones * (1.0f / 12.0f));
}

inline float MidiToFrequency(float midiNote) noexcept {
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) * (1.0f / 12.0f));
}

// Linear parameter interpolator over micro-blocks (24 or 32 samples)
class ParameterInterpolator {
public:
    ParameterInterpolator(float* state, float newValue, size_t size) noexcept
        : state_(state), value_(*state) {
        increment_ = (newValue - value_) / static_cast<float>(size);
    }

    ~ParameterInterpolator() noexcept {
        *state_ = value_;
    }

    inline float Next() noexcept {
        value_ += increment_;
        return value_;
    }

    inline float subsample(float t) const noexcept {
        return value_ + increment_ * t;
    }

private:
    float* state_;
    float value_;
    float increment_;
};

// One-pole smoother macros matching eurorack stmlib/dsp/dsp.h
#define ONE_POLE(out, in, coefficient) out += (coefficient) * ((in) - out);
#define SLOPE(out, in, positive, negative) { \
    float error = (in) - out; \
    out += (error > 0 ? positive : negative) * error; \
}

// Portable fast PRNG (xorshift32)
class Random {
public:
    void Init(uint32_t seed = 0x12345678u) noexcept { state_ = seed ? seed : 0x12345678u; }

    inline uint32_t GetWord() noexcept {
        uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state_ = x;
        return x;
    }

    inline float GetFloat() noexcept {
        return static_cast<float>(GetWord() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    inline float GetBipolar() noexcept {
        return GetFloat() * 2.0f - 1.0f;
    }

    // Approximate Gaussian in [-1, 1]
    inline float GetGaussian() noexcept {
        return (GetBipolar() + GetBipolar() + GetBipolar()) * 0.3333333f;
    }

private:
    uint32_t state_ = 0x5A3C9E21u;
};

// Zero-delay feedback Chamberlin/Andrew Simper State Variable Filter (used in Rings, Plaits, Clouds)
enum FilterMode {
    FILTER_MODE_LOW_PASS,
    FILTER_MODE_BAND_PASS,
    FILTER_MODE_BAND_PASS_NORMALIZED,
    FILTER_MODE_HIGH_PASS
};

class Svf {
public:
    void Init() noexcept {
        set_f_q(0.01f, 100.0f);
        Reset();
    }

    void Reset() noexcept {
        state_1_ = 0.0f;
        state_2_ = 0.0f;
    }

    inline void set_f_q(float f, float resonance) noexcept {
        f = std::clamp(f, 0.00005f, 0.495f);
        resonance = std::max(resonance, 0.5f);
        g_ = std::tan(kPi * f);
        r_ = 1.0f / resonance;
        h_ = 1.0f / (1.0f + r_ * g_ + g_ * g_);
    }

    // Polynomial tan() approximation (stmlib FREQUENCY_FAST) for per-sample cutoff sweeps
    inline void set_f_q_fast(float f, float resonance) noexcept {
        f = std::clamp(f, 0.00005f, 0.45f);
        resonance = std::max(resonance, 0.5f);
        constexpr float a = 3.260e-01f * kPi * kPi * kPi;
        constexpr float b = 1.823e-01f * kPi * kPi * kPi * kPi * kPi;
        const float f2 = f * f;
        g_ = f * (kPi + f2 * (a + b * f2));
        r_ = 1.0f / resonance;
        h_ = 1.0f / (1.0f + r_ * g_ + g_ * g_);
    }

    template <FilterMode mode>
    inline float Process(float in) noexcept {
        float hp = (in - r_ * state_1_ - g_ * state_1_ - state_2_) * h_;
        float bp = g_ * hp + state_1_;
        state_1_ = g_ * hp + bp;
        float lp = g_ * bp + state_2_;
        state_2_ = g_ * bp + lp;

        if constexpr (mode == FILTER_MODE_LOW_PASS) {
            return lp;
        } else if constexpr (mode == FILTER_MODE_BAND_PASS) {
            return bp;
        } else if constexpr (mode == FILTER_MODE_BAND_PASS_NORMALIZED) {
            return bp * r_;
        } else {
            return hp;
        }
    }

private:
    float g_ = 0.0f;
    float r_ = 1.0f;
    float h_ = 1.0f;
    float state_1_ = 0.0f;
    float state_2_ = 0.0f;
};

// DC Blocker
class DcBlocker {
public:
    void Init(float pole = 0.9992f) noexcept {
        pole_ = pole;
        Reset();
    }

    void Reset() noexcept {
        x_ = 0.0f;
        y_ = 0.0f;
    }

    inline float Process(float in) noexcept {
        float out = in - x_ + pole_ * y_;
        x_ = in;
        y_ = out;
        return out;
    }

private:
    float pole_ = 0.9992f;
    float x_ = 0.0f;
    float y_ = 0.0f;
};

// Griesinger / Dattorro All-Pass + Delay Diffusion Engine (Oliverb / Rings / Clouds Reverb)
class DiffusionReverb {
public:
    static constexpr size_t kBufferSize = 32768;
    static constexpr size_t kBufferMask = kBufferSize - 1;

    void Init() noexcept {
        buffer_.assign(kBufferSize, 0.0f);
        writePtr_ = 0;
        lpDecay1_ = 0.0f;
        lpDecay2_ = 0.0f;
        lfoPhase1_ = 0.0f;
        lfoPhase2_ = 0.37f;
    }

    void Clear() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        lpDecay1_ = 0.0f;
        lpDecay2_ = 0.0f;
    }

    void Process(float* left, float* right, size_t size,
                 float amount, float time, float diffusion, float lpCoeff, float inputGain = 0.25f) noexcept {
        const float kap = diffusion;
        const float klp = lpCoeff;
        const float krt = time;

        for (size_t i = 0; i < size; ++i) {
            writePtr_ = (writePtr_ - 1) & kBufferMask;

            lfoPhase1_ += 0.000018f;
            if (lfoPhase1_ >= 1.0f) lfoPhase1_ -= 1.0f;
            lfoPhase2_ += 0.000023f;
            if (lfoPhase2_ >= 1.0f) lfoPhase2_ -= 1.0f;

            float smL = std::sin(lfoPhase1_ * kTwoPi) * 32.0f;
            float smR = std::cos(lfoPhase2_ * kTwoPi) * 32.0f;

            float inMono = (left[i] + right[i]) * inputGain;

            // 4 Input Diffuser All-Pass Stages
            float acc = inMono;
            acc = ProcessAllPass(acc, 0,   142, kap);
            acc = ProcessAllPass(acc, 150, 107, kap);
            acc = ProcessAllPass(acc, 260, 379, kap);
            acc = ProcessAllPass(acc, 650, 277, kap);
            float apOut = acc;

            // Tank Branch A (Left)
            float tankBEnd = ReadInterp(21800 + smL);
            float tankA = apOut + tankBEnd * krt;
            ONE_POLE(lpDecay1_, tankA, klp);
            tankA = ProcessAllPass(lpDecay1_, 1000, 672, -kap);
            Write(1800, tankA);
            float wetL = Read(1800 + 2650) * 0.6f + Read(1800 + 4100) * 0.4f;

            // Tank Branch B (Right)
            float tankAEnd = ReadInterp(1800 + 4453 + smR);
            float tankB = apOut + tankAEnd * krt;
            ONE_POLE(lpDecay2_, tankB, klp);
            tankB = ProcessAllPass(lpDecay2_, 7000, 908, -kap);
            Write(8000, tankB);
            Write(16000, Read(8000 + 3720));
            float wetR = Read(8000 + 1990) * 0.6f + Read(16000 + 3100) * 0.4f;

            left[i]  += (wetL - left[i])  * amount;
            right[i] += (wetR - right[i]) * amount;
        }
    }

private:
    inline float Read(size_t offset) const noexcept {
        return buffer_[(writePtr_ + offset) & kBufferMask];
    }

    inline float ReadInterp(float offset) const noexcept {
        auto integral = static_cast<size_t>(offset);
        float frac = offset - static_cast<float>(integral);
        float a = Read(integral);
        float b = Read(integral + 1);
        return a + (b - a) * frac;
    }

    inline void Write(size_t offset, float val) noexcept {
        buffer_[(writePtr_ + offset) & kBufferMask] = val;
    }

    inline float ProcessAllPass(float in, size_t baseOffset, size_t length, float kap) noexcept {
        float delayed = Read(baseOffset + length);
        float v = in + delayed * kap;
        Write(baseOffset, v);
        return delayed - v * kap;
    }

    std::vector<float> buffer_;
    size_t writePtr_ = 0;
    float lpDecay1_ = 0.0f;
    float lpDecay2_ = 0.0f;
    float lfoPhase1_ = 0.0f;
    float lfoPhase2_ = 0.37f;
};

} // namespace stmlib
