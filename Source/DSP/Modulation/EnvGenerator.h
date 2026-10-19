#pragma once
#include <cmath>
#include <algorithm>

namespace ric::mod {

class EnvGenerator {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        reset();
    }

    void reset() noexcept {
        stage_ = Stage::Idle;
        level_ = 0.0f;
    }

    void noteOn() noexcept {
        stage_ = Stage::Attack;
    }

    void noteOff() noexcept {
        if (stage_ != Stage::Idle) {
            stage_ = Stage::Release;
        }
    }

    float stepSubBlock(float attackSec, float decaySec, float sustainLevel, float releaseSec, size_t numSamples) noexcept {
        const float dt = static_cast<float>(numSamples) / sampleRate_;
        const float sus = std::clamp(sustainLevel, 0.0f, 1.0f);

        switch (stage_) {
            case Stage::Idle:
                level_ = 0.0f;
                break;
            case Stage::Attack: {
                const float att = std::max(0.001f, attackSec);
                level_ += dt / att;
                if (level_ >= 1.0f) {
                    level_ = 1.0f;
                    stage_ = Stage::Decay;
                }
                break;
            }
            case Stage::Decay: {
                const float dec = std::max(0.005f, decaySec);
                const float coeff = std::exp(-dt * 4.5f / dec);
                level_ = sus + (level_ - sus) * coeff;
                if (std::abs(level_ - sus) < 0.001f) {
                    level_ = sus;
                    stage_ = Stage::Sustain;
                }
                break;
            }
            case Stage::Sustain:
                level_ = sus;
                break;
            case Stage::Release: {
                const float rel = std::max(0.01f, releaseSec);
                const float coeff = std::exp(-dt * 5.5f / rel);
                level_ *= coeff;
                if (level_ < 0.0002f) {
                    level_ = 0.0f;
                    stage_ = Stage::Idle;
                }
                break;
            }
        }
        return level_;
    }

    float getValue() const noexcept { return level_; }
    bool isActive() const noexcept { return stage_ != Stage::Idle; }
    bool isReleasing() const noexcept { return stage_ == Stage::Release || stage_ == Stage::Idle; }

private:
    float sampleRate_ = 48000.0f;
    Stage stage_ = Stage::Idle;
    float level_ = 0.0f;
};

} // namespace ric::mod
