#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include "../DSP/Modulation/ModMatrix.h"

namespace ric::core {

struct GrainVisualPoint {
    float delay = 0.0f;       // [0..1] age inside the cloud memory (0 = just heard, 1 = oldest)
    float pitchRatio = 1.0f;  // [0.25..4.0]
    float amplitude = 0.0f;   // [0..1]
    float pan = 0.5f;         // [0..1]
};

struct VoiceVisual {
    float note = 60.0f;
    float energy = 0.0f;
    bool active = false;
    bool gate = false;
};

// One frame of what the audio thread wants the UI to see (~60 per second)
struct VisualSnapshot {
    static constexpr size_t kScopeSamples = 128;
    static constexpr size_t kMaxVisualGrains = 32;
    static constexpr size_t kMemoryBins = 96;
    static constexpr size_t kMaxVoices = 16;
    static constexpr size_t kLoomSteps = 16;

    // Decimated scopes (~10 ms window)
    std::array<float, kScopeSamples> exciterWave{};
    std::array<float, kScopeSamples> outLeft{};
    std::array<float, kScopeSamples> outRight{};
    float peakLeft = 0.0f;
    float peakRight = 0.0f;

    // Live normalized [0..1] modulated values for the 20 modulatable knobs
    std::array<float, ric::mod::ModMatrix::kNumDests> modulatedNormalized{};
    // Live modulator outputs (bipolar for LFO / Loom, unipolar for ENV)
    std::array<float, ric::mod::ModMatrix::kNumSources> modSourceValues{};

    // Voices & resonator
    std::array<VoiceVisual, kMaxVoices> voices{};
    uint8_t activeVoices = 0;
    float resonatorEnergy = 0.0f;
    float exciterEnergy = 0.0f;
    float leadNote = 60.0f;

    // Clouds
    bool cloudsFrozen = false;
    int cloudsMode = 0;
    float cloudsPlayhead = 0.0f;   // knob position, same axis as grain delay
    uint8_t activeGrains = 0;
    std::array<GrainVisualPoint, kMaxVisualGrains> grains{};
    std::array<float, kMemoryBins> cloudsMemory{}; // [0] = newest

    // Generative Loom
    bool loomEnabled = false;
    uint8_t loomStep = 0;
    uint8_t loomLength = 8;
    bool loomTrigT1 = false;   // latched since the previous frame
    bool loomTrigT3 = false;
    std::array<float, kLoomSteps> loomStepsX1{};
    float loomX1 = 0.0f;
    float loomY = 0.0f;
};

class LockFreeVisualFifo {
public:
    static constexpr int kCapacity = 8;

    void push(const VisualSnapshot& snapshot) noexcept {
        const auto scope = fifo_.write(1);
        if (scope.blockSize1 > 0) {
            buffer_[static_cast<size_t>(scope.startIndex1)] = snapshot;
        }
    }

    bool popLatest(VisualSnapshot& outSnapshot) noexcept {
        const int numReady = fifo_.getNumReady();
        if (numReady <= 0)
            return false;

        const auto scope = fifo_.read(numReady);
        if (scope.blockSize2 > 0) {
            outSnapshot = buffer_[static_cast<size_t>(scope.startIndex2 + scope.blockSize2 - 1)];
        } else if (scope.blockSize1 > 0) {
            outSnapshot = buffer_[static_cast<size_t>(scope.startIndex1 + scope.blockSize1 - 1)];
        }
        return true;
    }

private:
    juce::AbstractFifo fifo_{ kCapacity };
    std::array<VisualSnapshot, kCapacity> buffer_{};
};

} // namespace ric::core
