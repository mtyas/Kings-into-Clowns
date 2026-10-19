#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include <atomic>
#include <functional>

namespace ric::core {

// A JUCE AudioParameterFloat that natively implements CLAP non-destructive monophonic
// and per-note-ID polyphonic modulation via clap-juce-extensions.
class ClapPolyParameter : public juce::AudioParameterFloat,
                          public clap_juce_extensions::clap_juce_parameter_capabilities {
public:
    using PolyModCallback = std::function<void(int paramTag, int32_t noteId, int16_t channel, int16_t key, float amount)>;

    ClapPolyParameter(const juce::ParameterID& parameterID,
                      const juce::String& parameterName,
                      juce::NormalisableRange<float> normalisableRange,
                      float defaultValue,
                      int paramTag,
                      bool supportsPolyMod = true,
                      const juce::AudioParameterFloatAttributes& attributes = {})
        : juce::AudioParameterFloat(parameterID, parameterName, normalisableRange, defaultValue, attributes),
          paramTag_(paramTag),
          supportsPolyMod_(supportsPolyMod) {}

    ~ClapPolyParameter() override = default;

    void setPolyModCallback(PolyModCallback cb) {
        polyModCallback_ = std::move(cb);
    }

    int getParamTag() const noexcept { return paramTag_; }
    float getMonoModulationOffset() const noexcept { return monoModOffset_.load(std::memory_order_relaxed); }

    // clap_juce_parameter_capabilities overrides
    bool supportsMonophonicModulation() override { return true; }

    void applyMonophonicModulation(double amount) override {
        monoModOffset_.store(static_cast<float>(amount), std::memory_order_relaxed);
    }

    bool supportsPolyphonicModulation() override { return supportsPolyMod_; }

    void applyPolyphonicModulation(int32_t note_id, int16_t /*port_index*/,
                                   int16_t channel, int16_t key, double amount) override {
        if (note_id < 0 && key < 0) {
            monoModOffset_.store(static_cast<float>(amount), std::memory_order_relaxed);
            return;
        }
        if (polyModCallback_) {
            polyModCallback_(paramTag_, note_id, channel, key, static_cast<float>(amount));
        }
    }

private:
    int paramTag_ = 0;
    bool supportsPolyMod_ = true;
    std::atomic<float> monoModOffset_{ 0.0f };
    PolyModCallback polyModCallback_;
};

} // namespace ric::core
