#pragma once
#include "../../Core/StmDspCore.h"
#include "../../Core/LockFreeVisualFifo.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace ric::dsp {

struct CloudsPatch {
    int mode = 0;            // 0=Granular, 1=Pitch/Stretch, 2=Looping Delay, 3=Spectral Warp
    bool freeze = false;     // Freeze circular buffer
    float position = 0.35f;  // [0..1] Scrub position / Delay time
    float size = 0.6f;       // [0..1] Grain size / Window size
    float pitch = 0.0f;      // [-24..+24] semitones
    float density = 0.7f;    // [0..1] 0.5 = quiet, <0.5 deterministic, >0.5 random granular
    float texture = 0.65f;   // [0..1] Window morph (boxcar->triangle->hann->smooth) + diffuser
    float feedback = 0.35f;  // [0..1]
    float reverb = 0.6f;     // [0..1] Oliverb tail amount
    float stereoSpread = 0.8f; // [0..1]
    float dryWet = 0.45f;    // [0..1]
};

// Mutable Instruments Clouds / Beads Granular Texture Synthesizer & Oliverb Diffusion Engine
class CloudsWrapper {
public:
    static constexpr size_t kBufferSamples = 262144; // ~5.4 seconds stereo at 48kHz
    static constexpr size_t kBufferMask = kBufferSamples - 1;
    static constexpr size_t kMaxGrains = 40;

    struct Grain {
        bool active = false;
        double readPos = 0.0;     // absolute position in the circular buffer, kept in [0, kBufferSamples)
        float phase = 0.0f;
        float phaseInc = 0.001f;
        float pitchRatio = 1.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        float windowShape = 0.5f;
        float gain = 1.0f;
    };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        invSampleRate_ = 1.0f / sampleRate_;
        historySamples_ = std::min(sampleRate_ * 3.2f, static_cast<float>(kBufferSamples - 4096));
        memoryBinLength_ = std::max(1, static_cast<int>(historySamples_ / static_cast<float>(ric::core::VisualSnapshot::kMemoryBins)));
        bufferL_.assign(kBufferSamples, 0.0f);
        bufferR_.assign(kBufferSamples, 0.0f);
        rng_.Init(0xC10AD501u);
        reverb_.Init();
        fbFilterL_.Init();
        fbFilterR_.Init();
        dcL_.Init(0.9993f);
        dcR_.Init(0.9993f);
        for (auto& b : spectralBandsL_) b.Init();
        for (auto& b : spectralBandsR_) b.Init();
        reset();
    }

    void reset() noexcept {
        std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
        for (auto& g : grains_) g.active = false;
        writePtr_ = 0;
        grainSpawnCounter_ = 0.0f;
        wsolaPhaseA_ = 0.0f;
        wsolaPhaseB_ = 0.5f;
        memory_.fill(0.0f);
        memoryWrite_ = 0;
        memoryCounter_ = 0;
        memoryPeak_ = 0.0f;
        tapCount_ = 0;
        reverb_.Clear();
        fbFilterL_.Reset();
        fbFilterR_.Reset();
        dcL_.Reset();
        dcR_.Reset();
    }

    void triggerGrainSeed() noexcept {
        grainSpawnCounter_ = 1.0f;
    }

    void processSubBlock(const CloudsPatch& patch,
                         const float* inL, const float* inR,
                         float* outL, float* outR,
                         size_t size) noexcept {
        const float pos   = std::clamp(patch.position, 0.0f, 0.995f);
        const float sz    = std::clamp(patch.size, 0.0f, 1.0f);
        const float dens  = std::clamp(patch.density, 0.0f, 1.0f);
        const float tex   = std::clamp(patch.texture, 0.0f, 1.0f);
        const float fb    = std::clamp(patch.feedback, 0.0f, 0.95f);
        const float rev   = std::clamp(patch.reverb, 0.0f, 1.0f);
        const float sprd  = std::clamp(patch.stereoSpread, 0.0f, 1.0f);
        const float mix   = std::clamp(patch.dryWet, 0.0f, 1.0f);
        const float pitchRatio = stmlib::SemitonesToRatio(std::clamp(patch.pitch, -24.0f, 24.0f));

        fbFilterL_.set_f_q(0.18f + 0.25f * (1.0f - tex), 0.7f);
        fbFilterR_.set_f_q(0.18f + 0.25f * (1.0f - tex), 0.7f);

        float wetL[32]{};
        float wetR[32]{};

        tapCount_ = 0;
        switch (patch.mode) {
            case 0: // I. Classic Granular Texture Engine
                processGranular(patch.freeze, pos, sz, pitchRatio, dens, tex, fb, sprd, inL, inR, wetL, wetR, size);
                break;
            case 1: // II. WSOLA Pitch Shifter / Time Stretcher
                processPitchShifter(patch.freeze, pos, sz, pitchRatio, dens, tex, fb, sprd, inL, inR, wetL, wetR, size);
                break;
            case 2: // III. Looping Tap Delay / Beat-Repeat
                processLoopingDelay(patch.freeze, pos, sz, pitchRatio, dens, tex, fb, sprd, inL, inR, wetL, wetR, size);
                break;
            case 3: // IV. Spectral Phase Warp / Resonant Filter Bank
            default:
                processSpectralWarp(patch.freeze, pos, sz, pitchRatio, dens, tex, fb, sprd, inL, inR, wetL, wetR, size);
                break;
        }

        // Oliverb Ambient Diffusion Network
        if (rev > 0.001f) {
            const float revTime = 0.55f + rev * 0.42f;
            const float revDiff = 0.62f + tex * 0.16f;
            const float revLp   = 0.35f + (1.0f - tex * 0.4f) * 0.5f;
            reverb_.Process(wetL, wetR, size, rev * 0.75f, revTime, revDiff, revLp, 0.32f);
        }

        // Equal-power Dry/Wet crossfade
        const float dryGain = std::cos(mix * stmlib::kPi * 0.5f);
        const float wetGain = std::sin(mix * stmlib::kPi * 0.5f);

        for (size_t i = 0; i < size; ++i) {
            outL[i] = inL[i] * dryGain + wetL[i] * wetGain;
            outR[i] = inR[i] * dryGain + wetR[i] * wetGain;
        }
    }

    // Populate UI telemetry snapshot without locks
    void fillVisualTelemetry(ric::core::VisualSnapshot& snap, const CloudsPatch& patch) const noexcept {
        using Snap = ric::core::VisualSnapshot;
        snap.cloudsFrozen = patch.freeze;
        snap.cloudsMode = patch.mode;
        // Every mode maps "position" onto the same memory axis the UI draws
        snap.cloudsPlayhead = std::clamp(patch.position, 0.0f, 1.0f);

        uint8_t count = 0;
        auto addPoint = [&](float delaySamples, float pitch, float amp, float pan) {
            if (count >= Snap::kMaxVisualGrains) return;
            auto& vp = snap.grains[count++];
            vp.delay = std::clamp(delaySamples / historySamples_, 0.0f, 1.0f);
            vp.pitchRatio = pitch;
            vp.amplitude = std::clamp(amp, 0.0f, 1.0f);
            vp.pan = pan;
        };

        if (patch.mode == 0 || patch.mode == 3) {
            for (const auto& g : grains_) {
                if (!g.active) continue;
                const double age = std::fmod(static_cast<double>(writePtr_) - g.readPos + 2.0 * kBufferSamples,
                                             static_cast<double>(kBufferSamples));
                addPoint(static_cast<float>(age), g.pitchRatio,
                         evaluateGrainWindow(g.phase, g.windowShape) * g.gain * 1.6f,
                         g.panR / std::max(0.01f, g.panL + g.panR));
            }
        } else {
            for (int t = 0; t < tapCount_; ++t)
                addPoint(tapDelay_[static_cast<size_t>(t)], tapPitch_, tapAmp_[static_cast<size_t>(t)], t == 0 ? 0.35f : 0.65f);
        }
        snap.activeGrains = count;

        for (size_t k = 0; k < Snap::kMemoryBins; ++k) {
            snap.cloudsMemory[k] = memory_[(memoryWrite_ + Snap::kMemoryBins - 1 - k) % Snap::kMemoryBins];
        }
    }

private:
    static inline float evaluateGrainWindow(float phase, float shape) noexcept {
        phase = std::clamp(phase, 0.0f, 1.0f);
        if (shape < 0.33f) {
            // Trapezoid / Boxcar to Triangle
            const float edge = std::max(0.02f, shape * 1.5f);
            if (phase < edge) return phase / edge;
            if (phase > 1.0f - edge) return (1.0f - phase) / edge;
            return 1.0f;
        } else if (shape < 0.66f) {
            // Triangle to Hann
            const float tri = 1.0f - std::abs(2.0f * phase - 1.0f);
            const float hann = 0.5f - 0.5f * std::cos(phase * stmlib::kTwoPi);
            return stmlib::Crossfade(tri, hann, (shape - 0.33f) * 3.03f);
        } else {
            // Smooth raised cosine bell
            const float hann = 0.5f - 0.5f * std::cos(phase * stmlib::kTwoPi);
            return hann * hann * 1.25f;
        }
    }

    void spawnGrain(float pos, float sizeParam, float pitchRatio, float texture, float spread) noexcept {
        // Find free or quietest grain slot
        size_t targetIdx = 0;
        float minAmp = 100.0f;
        for (size_t i = 0; i < kMaxGrains; ++i) {
            if (!grains_[i].active) {
                targetIdx = i;
                break;
            }
            const float edgeDist = std::min(grains_[i].phase, 1.0f - grains_[i].phase);
            if (edgeDist < minAmp) {
                minAmp = edgeDist;
                targetIdx = i;
            }
        }

        auto& g = grains_[targetIdx];
        g.active = true;
        g.phase = 0.0f;

        // Grain duration: 15ms to 650ms
        const float durationSamples = (0.015f + sizeParam * sizeParam * 0.635f) * sampleRate_;
        g.phaseInc = 1.0f / std::max(64.0f, durationSamples);
        g.pitchRatio = pitchRatio;
        g.windowShape = texture;

        // Position in circular history buffer (up to ~3.2 seconds back)
        const float jitter = rng_.GetBipolar() * sampleRate_ * 0.045f * (0.2f + sizeParam);
        const float delaySamples = std::clamp(pos * historySamples_ + jitter + durationSamples * std::max(0.0f, pitchRatio - 1.0f) + 64.0f,
                                              64.0f, static_cast<float>(kBufferSamples - 2048));
        g.readPos = std::fmod(static_cast<double>(writePtr_) - static_cast<double>(delaySamples) + kBufferSamples,
                              static_cast<double>(kBufferSamples));

        const float panAngle = std::clamp(0.5f + rng_.GetBipolar() * 0.5f * spread, 0.0f, 1.0f) * (stmlib::kPi * 0.5f);
        g.panL = std::cos(panAngle);
        g.panR = std::sin(panAngle);
        g.gain = 0.55f;
    }

    void processGranular(bool freeze, float pos, float sz, float pitchRatio, float dens, float tex,
                         float fb, float sprd, const float* inL, const float* inR, float* wetL, float* wetR, size_t size) noexcept {
        // Compute grain trigger rate from bipolar density around 0.5
        const float densDev = std::abs(dens - 0.5f) * 2.0f;
        const bool stochastic = (dens >= 0.5f);
        const float grainDuration = (0.015f + sz * sz * 0.635f) * sampleRate_;
        const float overlap = 0.5f + densDev * 10.5f;
        const float spawnProbPerSample = overlap / std::max(64.0f, grainDuration);

        for (size_t i = 0; i < size; ++i) {
            if (stochastic) {
                if (rng_.GetFloat() < spawnProbPerSample) {
                    spawnGrain(pos, sz, pitchRatio, tex, sprd);
                }
            } else {
                grainSpawnCounter_ += spawnProbPerSample;
                if (grainSpawnCounter_ >= 1.0f) {
                    grainSpawnCounter_ -= 1.0f;
                    spawnGrain(pos, sz, pitchRatio, tex, sprd);
                }
            }

            float sumL = 0.0f;
            float sumR = 0.0f;
            int activeCount = 0;

            for (auto& g : grains_) {
                if (!g.active) continue;
                ++activeCount;

                const float win = evaluateGrainWindow(g.phase, g.windowShape) * g.gain;
                const float sL = stmlib::InterpolateHermite(bufferL_.data(), g.readPos, static_cast<int32_t>(kBufferMask));
                const float sR = stmlib::InterpolateHermite(bufferR_.data(), g.readPos, static_cast<int32_t>(kBufferMask));
                g.readPos += g.pitchRatio;
                if (g.readPos >= static_cast<double>(kBufferSamples)) g.readPos -= static_cast<double>(kBufferSamples);

                sumL += sL * win * g.panL;
                sumR += sR * win * g.panR;

                g.phase += g.phaseInc;
                if (g.phase >= 1.0f) {
                    g.active = false;
                }
            }

            const float norm = 1.0f / std::sqrt(static_cast<float>(std::max(1, activeCount)) * 0.45f + 1.0f);
            float outSampleL = stmlib::SoftLimit(sumL * norm);
            float outSampleR = stmlib::SoftLimit(sumR * norm);

            // Write input + filtered feedback to circular buffer unless frozen
            if (!freeze) {
                const float fbL = dcL_.Process(fbFilterL_.Process<stmlib::FILTER_MODE_LOW_PASS>(outSampleL * fb));
                const float fbR = dcR_.Process(fbFilterR_.Process<stmlib::FILTER_MODE_LOW_PASS>(outSampleR * fb));
                writeToBuffer(inL[i] + fbL, inR[i] + fbR);
            }

            wetL[i] = outSampleL;
            wetR[i] = outSampleR;
        }
    }

    void processPitchShifter(bool freeze, float pos, float sz, float pitchRatio, float /*dens*/, float tex,
                             float fb, float sprd, const float* inL, const float* inR, float* wetL, float* wetR, size_t size) noexcept {
        const float winSamples = (0.025f + sz * 0.18f) * sampleRate_;
        const float baseDelay = (0.01f + pos * 1.2f) * sampleRate_;
        const float phaseStep = std::abs(1.0f - pitchRatio) / winSamples + 0.00005f;

        for (size_t i = 0; i < size; ++i) {
            wsolaPhaseA_ += phaseStep;
            if (wsolaPhaseA_ >= 1.0f) wsolaPhaseA_ -= 1.0f;
            wsolaPhaseB_ = std::fmod(wsolaPhaseA_ + 0.5f, 1.0f);

            const float winA = 0.5f - 0.5f * std::cos(wsolaPhaseA_ * stmlib::kTwoPi);
            const float winB = 0.5f - 0.5f * std::cos(wsolaPhaseB_ * stmlib::kTwoPi);

            const float dA = baseDelay + ((pitchRatio > 1.0f) ? (1.0f - wsolaPhaseA_) : wsolaPhaseA_) * winSamples;
            const float dB = baseDelay + ((pitchRatio > 1.0f) ? (1.0f - wsolaPhaseB_) : wsolaPhaseB_) * winSamples;

            const double idxA = static_cast<double>(writePtr_ + kBufferSamples) - dA;
            const double idxB = static_cast<double>(writePtr_ + kBufferSamples) - dB;
            const double spreadOffset = sprd * 48.0;
            constexpr auto mask = static_cast<int32_t>(kBufferMask);

            const float sL = stmlib::InterpolateHermite(bufferL_.data(), idxA, mask) * winA
                           + stmlib::InterpolateHermite(bufferL_.data(), idxB, mask) * winB;
            const float sR = stmlib::InterpolateHermite(bufferR_.data(), idxA - spreadOffset, mask) * winA
                           + stmlib::InterpolateHermite(bufferR_.data(), idxB + spreadOffset, mask) * winB;

            if (i + 1 == size) {
                tapCount_ = 2;
                tapPitch_ = pitchRatio;
                tapDelay_[0] = dA; tapAmp_[0] = winA;
                tapDelay_[1] = dB; tapAmp_[1] = winB;
            }

            if (!freeze) {
                writeToBuffer(inL[i] + sL * fb * (0.5f + 0.4f * tex), inR[i] + sR * fb * (0.5f + 0.4f * tex));
            }

            wetL[i] = stmlib::SoftLimit(sL);
            wetR[i] = stmlib::SoftLimit(sR);
        }
    }

    void processLoopingDelay(bool freeze, float pos, float sz, float pitchRatio, float /*dens*/, float tex,
                             float fb, float sprd, const float* inL, const float* inR, float* wetL, float* wetR, size_t size) noexcept {
        const float loopLen = (0.04f + sz * 1.8f) * sampleRate_ / std::max(0.25f, pitchRatio);
        const float startOffset = (0.01f + pos * 1.5f) * sampleRate_;

        for (size_t i = 0; i < size; ++i) {
            wsolaPhaseA_ += pitchRatio / loopLen;
            if (wsolaPhaseA_ >= 1.0f) wsolaPhaseA_ -= 1.0f;

            const float readDelay = startOffset + (1.0f - wsolaPhaseA_) * loopLen;
            const double idxL = static_cast<double>(writePtr_ + kBufferSamples) - readDelay;
            const double idxR = idxL - sprd * 320.0;
            constexpr auto mask = static_cast<int32_t>(kBufferMask);

            const float env = evaluateGrainWindow(wsolaPhaseA_, 0.25f + tex * 0.5f);
            const float sL = stmlib::InterpolateHermite(bufferL_.data(), idxL, mask) * env;
            const float sR = stmlib::InterpolateHermite(bufferR_.data(), idxR, mask) * env;

            if (i + 1 == size) {
                tapCount_ = 1;
                tapPitch_ = pitchRatio;
                tapDelay_[0] = readDelay; tapAmp_[0] = env;
            }

            if (!freeze) {
                writeToBuffer(inL[i] + sR * fb, inR[i] + sL * fb);
            }

            wetL[i] = sL;
            wetR[i] = sR;
        }
    }

    void processSpectralWarp(bool freeze, float pos, float sz, float pitchRatio, float dens, float tex,
                             float fb, float sprd, const float* inL, const float* inR, float* wetL, float* wetR, size_t size) noexcept {
        // First run granular engine, then pass through a resonant 8-band spectral phase/formant bank
        processGranular(freeze, pos, sz, pitchRatio, dens, tex, fb, sprd, inL, inR, wetL, wetR, size);

        static constexpr std::array<float, 8> kBandFreqs = {
            160.0f, 320.0f, 640.0f, 1100.0f, 1900.0f, 3200.0f, 5400.0f, 8800.0f
        };
        const float q = 1.5f + tex * 6.5f;
        const float shift = stmlib::SemitonesToRatio((pos - 0.5f) * 18.0f);

        for (size_t b = 0; b < 8; ++b) {
            const float f = std::clamp(kBandFreqs[b] * shift * invSampleRate_, 0.002f, 0.45f);
            spectralBandsL_[b].set_f_q(f, q);
            spectralBandsR_[b].set_f_q(std::clamp(f * (1.0f + sprd * 0.06f), 0.002f, 0.45f), q);
        }

        for (size_t i = 0; i < size; ++i) {
            float accL = 0.0f;
            float accR = 0.0f;
            for (size_t b = 0; b < 8; ++b) {
                const float weight = (b & 1) ? (0.4f + sz * 0.6f) : (1.0f - sz * 0.4f);
                accL += spectralBandsL_[b].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(wetL[i]) * weight;
                accR += spectralBandsR_[b].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(wetR[i]) * weight;
            }
            wetL[i] = stmlib::Crossfade(wetL[i], stmlib::SoftLimit(accL * 0.65f), 0.65f);
            wetR[i] = stmlib::Crossfade(wetR[i], stmlib::SoftLimit(accR * 0.65f), 0.65f);
        }
    }

    // Writes one stereo frame into the circular memory and keeps the coarse envelope the UI draws
    inline void writeToBuffer(float l, float r) noexcept {
        l = stmlib::SoftLimit(l);
        r = stmlib::SoftLimit(r);
        bufferL_[writePtr_] = l;
        bufferR_[writePtr_] = r;
        writePtr_ = (writePtr_ + 1) & kBufferMask;

        memoryPeak_ = std::max(memoryPeak_, std::max(std::abs(l), std::abs(r)));
        if (++memoryCounter_ >= memoryBinLength_) {
            memory_[memoryWrite_] = memoryPeak_;
            memoryWrite_ = (memoryWrite_ + 1) % ric::core::VisualSnapshot::kMemoryBins;
            memoryCounter_ = 0;
            memoryPeak_ = 0.0f;
        }
    }

    float sampleRate_ = 48000.0f;
    float invSampleRate_ = 1.0f / 48000.0f;
    float historySamples_ = 48000.0f * 3.2f;

    std::array<float, ric::core::VisualSnapshot::kMemoryBins> memory_{};
    size_t memoryWrite_ = 0;
    int memoryCounter_ = 0;
    int memoryBinLength_ = 1600;
    float memoryPeak_ = 0.0f;

    int tapCount_ = 0;
    float tapPitch_ = 1.0f;
    std::array<float, 2> tapDelay_{};
    std::array<float, 2> tapAmp_{};

    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    size_t writePtr_ = 0;

    std::array<Grain, kMaxGrains> grains_{};
    float grainSpawnCounter_ = 0.0f;
    float wsolaPhaseA_ = 0.0f;
    float wsolaPhaseB_ = 0.5f;

    stmlib::Random rng_;
    stmlib::DiffusionReverb reverb_;
    stmlib::Svf fbFilterL_;
    stmlib::Svf fbFilterR_;
    stmlib::DcBlocker dcL_;
    stmlib::DcBlocker dcR_;
    std::array<stmlib::Svf, 8> spectralBandsL_{};
    std::array<stmlib::Svf, 8> spectralBandsR_{};
};

} // namespace ric::dsp
