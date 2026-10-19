#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>

namespace ric::core {

class MidiLearnManager {
public:
    MidiLearnManager() {
        for (auto& slot : ccToParamIdx_) {
            slot.store(-1, std::memory_order_relaxed);
        }
    }

    void bindParameterList(const juce::Array<juce::AudioProcessorParameter*>& params) {
        std::lock_guard<std::mutex> lock(mutex_);
        params_.clear();
        paramIdToIndex_.clear();
        for (int i = 0; i < params.size(); ++i) {
            params_.push_back(params[i]);
            if (auto* pWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[i])) {
                paramIdToIndex_[pWithId->paramID] = i;
            }
        }
    }

    void setLearnActive(bool active) noexcept {
        learnActive_.store(active, std::memory_order_relaxed);
        if (!active) {
            learningParamIdx_.store(-1, std::memory_order_relaxed);
        }
    }

    bool isLearnActive() const noexcept {
        return learnActive_.load(std::memory_order_relaxed);
    }

    void setTargetParameterForLearn(const juce::String& paramId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = paramIdToIndex_.find(paramId);
        if (it != paramIdToIndex_.end()) {
            learningParamIdx_.store(it->second, std::memory_order_relaxed);
            learnActive_.store(true, std::memory_order_relaxed);
        }
    }

    juce::String getLearningParamId() const {
        const int idx = learningParamIdx_.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex_);
        if (idx >= 0 && idx < static_cast<int>(params_.size())) {
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(params_[static_cast<size_t>(idx)]))
                return p->paramID;
        }
        return {};
    }

    int getMappedCCForParam(const juce::String& paramId) const {
        int pIdx = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = paramIdToIndex_.find(paramId);
            if (it == paramIdToIndex_.end()) return -1;
            pIdx = it->second;
        }
        for (int cc = 0; cc < 128; ++cc) {
            if (ccToParamIdx_[static_cast<size_t>(cc)].load(std::memory_order_relaxed) == pIdx)
                return cc;
        }
        return -1;
    }

    void clearMappingForParam(const juce::String& paramId) {
        int pIdx = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = paramIdToIndex_.find(paramId);
            if (it == paramIdToIndex_.end()) return;
            pIdx = it->second;
        }
        for (int cc = 0; cc < 128; ++cc) {
            int expected = pIdx;
            ccToParamIdx_[static_cast<size_t>(cc)].compare_exchange_strong(expected, -1, std::memory_order_relaxed);
        }
    }

    void clearAllMappings() noexcept {
        for (auto& slot : ccToParamIdx_) {
            slot.store(-1, std::memory_order_relaxed);
        }
    }

    // Called on the audio thread when a MIDI CC arrives
    bool processMidiCC(int ccNumber, float normalizedValue) noexcept {
        if (ccNumber < 0 || ccNumber >= 128) return false;

        // 1. If waiting to learn a knob, bind this CC to that knob
        if (learnActive_.load(std::memory_order_relaxed)) {
            const int targetIdx = learningParamIdx_.load(std::memory_order_relaxed);
            if (targetIdx >= 0) {
                // Remove old CC bindings to this parameter
                for (int c = 0; c < 128; ++c) {
                    int exp = targetIdx;
                    ccToParamIdx_[static_cast<size_t>(c)].compare_exchange_strong(exp, -1, std::memory_order_relaxed);
                }
                ccToParamIdx_[static_cast<size_t>(ccNumber)].store(targetIdx, std::memory_order_relaxed);
                learningParamIdx_.store(-1, std::memory_order_relaxed);
                learnActive_.store(false, std::memory_order_relaxed);
                return true;
            }
        }

        // 2. Apply mapped CC to target parameter
        const int paramIdx = ccToParamIdx_[static_cast<size_t>(ccNumber)].load(std::memory_order_relaxed);
        if (paramIdx >= 0 && paramIdx < static_cast<int>(params_.size())) {
            if (auto* p = params_[static_cast<size_t>(paramIdx)]) {
                p->setValueNotifyingHost(std::clamp(normalizedValue, 0.0f, 1.0f));
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<juce::XmlElement> saveToXml() const {
        auto xml = std::make_unique<juce::XmlElement>("MidiLearnMappings");
        std::lock_guard<std::mutex> lock(mutex_);
        for (int cc = 0; cc < 128; ++cc) {
            const int idx = ccToParamIdx_[static_cast<size_t>(cc)].load(std::memory_order_relaxed);
            if (idx >= 0 && idx < static_cast<int>(params_.size())) {
                if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(params_[static_cast<size_t>(idx)])) {
                    auto* item = xml->createNewChildElement("Map");
                    item->setAttribute("cc", cc);
                    item->setAttribute("paramId", p->paramID);
                }
            }
        }
        return xml;
    }

    void loadFromXml(const juce::XmlElement* xml) {
        if (!xml) return;
        clearAllMappings();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* item : xml->getChildWithTagNameIterator("Map")) {
            const int cc = item->getIntAttribute("cc", -1);
            const juce::String paramId = item->getStringAttribute("paramId");
            if (cc >= 0 && cc < 128) {
                auto it = paramIdToIndex_.find(paramId);
                if (it != paramIdToIndex_.end()) {
                    ccToParamIdx_[static_cast<size_t>(cc)].store(it->second, std::memory_order_relaxed);
                }
            }
        }
    }

private:
    mutable std::mutex mutex_;
    std::vector<juce::AudioProcessorParameter*> params_;
    std::map<juce::String, int> paramIdToIndex_;
    std::array<std::atomic<int>, 128> ccToParamIdx_{};
    std::atomic<bool> learnActive_{ false };
    std::atomic<int> learningParamIdx_{ -1 };
};

} // namespace ric::core
