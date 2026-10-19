#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FactoryPresets.h"
#include "../DSP/Modulation/ModMatrix.h"
#include <vector>

namespace ric::core {

class PresetManager {
public:
    static constexpr const char* kPresetExtension = ".kicpreset";
    static constexpr const char* kPresetPattern = "*.kicpreset;*.ricpreset"; // older patches still load

    PresetManager(juce::AudioProcessorValueTreeState& apvts,
                  ric::mod::ModMatrix& modMatrix,
                  juce::UndoManager& undoManager)
        : apvts_(apvts), modMatrix_(modMatrix), undoManager_(undoManager) {
        userPresetDir_ = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("mtyas")
                             .getChildFile("KingsIntoClowns")
                             .getChildFile("Presets");
        if (!userPresetDir_.exists()) {
            userPresetDir_.createDirectory();
            // Bring along patches saved under the plugin's former name
            const auto legacyDir = userPresetDir_.getParentDirectory().getParentDirectory()
                                       .getChildFile("RingsIntoClouds").getChildFile("Presets");
            for (const auto& f : legacyDir.findChildFiles(juce::File::findFiles, false, "*.ricpreset"))
                f.copyFileTo(userPresetDir_.getChildFile(f.getFileName()));
        }
        rescanPresets();
    }

    void rescanPresets() {
        presets_ = ric::presets::getFactoryPresets();

        if (userPresetDir_.isDirectory()) {
            auto files = userPresetDir_.findChildFiles(juce::File::findFiles, false, kPresetPattern);
            files.sort();
            for (const auto& f : files) {
                if (auto xml = juce::XmlDocument::parse(f)) {
                    ric::presets::FactoryPreset up;
                    up.name = xml->getStringAttribute("name", f.getFileNameWithoutExtension());
                    up.category = xml->getStringAttribute("category", "User");
                    up.poemLine = xml->getStringAttribute("poem", "Custom user patch by mtyas.");
                    up.isFactory = false;
                    up.userFile = f;

                    if (auto* paramsXml = xml->getChildByName("Parameters")) {
                        for (auto* p : paramsXml->getChildWithTagNameIterator("Param")) {
                            up.values[p->getStringAttribute("id")] = static_cast<float>(p->getDoubleAttribute("val", 0.0));
                        }
                    }
                    presets_.push_back(std::move(up));
                }
            }
        }

        categories_.clear();
        categories_.add("All");
        for (const auto& p : presets_) {
            if (!categories_.contains(p.category))
                categories_.add(p.category);
        }
        if (!categories_.contains("User"))
            categories_.add("User");
    }

    int getNumPresets() const noexcept { return static_cast<int>(presets_.size()); }
    int getCurrentIndex() const noexcept { return currentIndex_; }

    // Restores which preset is "current" (e.g. from saved session state) without touching parameters
    void setCurrentIndexSilently(int index) noexcept {
        if (!presets_.empty())
            currentIndex_ = std::clamp(index, 0, static_cast<int>(presets_.size()) - 1);
    }
    const std::vector<ric::presets::FactoryPreset>& getAllPresets() const noexcept { return presets_; }
    const juce::StringArray& getCategories() const noexcept { return categories_; }
    juce::File getUserPresetDirectory() const noexcept { return userPresetDir_; }

    const ric::presets::FactoryPreset* getCurrentPreset() const noexcept {
        if (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(presets_.size()))
            return &presets_[static_cast<size_t>(currentIndex_)];
        return nullptr;
    }

    void loadPreset(int index) {
        if (presets_.empty()) return;
        currentIndex_ = std::clamp(index, 0, static_cast<int>(presets_.size()) - 1);
        const auto& preset = presets_[static_cast<size_t>(currentIndex_)];

        undoManager_.beginNewTransaction("Load Preset: " + preset.name);

        for (const auto& [paramId, val] : preset.values) {
            if (auto* param = apvts_.getParameter(paramId)) {
                param->setValueNotifyingHost(param->convertTo0to1(val));
            }
        }

        // Anything a preset does not mention (e.g. parameters added after it was written)
        // falls back to its default, so presets always sound the same.
        for (auto* p : apvts_.processor.getParameters()) {
            if (auto* withId = dynamic_cast<juce::RangedAudioParameter*>(p)) {
                if (preset.values.find(withId->paramID) == preset.values.end())
                    withId->setValueNotifyingHost(withId->getDefaultValue());
            }
        }

        if (!preset.isFactory && preset.userFile.existsAsFile()) {
            if (auto xml = juce::XmlDocument::parse(preset.userFile)) {
                if (auto* modXml = xml->getChildByName("ModMatrix")) {
                    loadModMatrixFromXml(modXml);
                }
            }
        } else {
            modMatrix_.initDefaultPoeticRouting();
        }
    }

    void stepPreset(int delta, const juce::String& categoryFilter = "All") {
        if (presets_.empty()) return;
        const int n = static_cast<int>(presets_.size());
        int idx = currentIndex_;
        for (int attempt = 0; attempt < n; ++attempt) {
            idx = (idx + delta + n) % n;
            if (categoryFilter == "All" || presets_[static_cast<size_t>(idx)].category == categoryFilter) {
                loadPreset(idx);
                return;
            }
        }
    }

    bool saveUserPreset(const juce::String& name, const juce::String& category, const juce::String& poemLine) {
        if (name.trim().isEmpty()) return false;
        const juce::String safeName = juce::File::createLegalFileName(name.trim());
        const juce::File targetFile = userPresetDir_.getChildFile(safeName + kPresetExtension);
        if (!savePresetToFile(targetFile, name.trim(), category.trim().isEmpty() ? "User" : category.trim(), poemLine))
            return false;

        rescanPresets();
        for (size_t i = 0; i < presets_.size(); ++i) {
            if (presets_[i].userFile == targetFile) {
                currentIndex_ = static_cast<int>(i);
                break;
            }
        }
        return true;
    }

    bool savePresetToFile(const juce::File& file, const juce::String& name, const juce::String& category, const juce::String& poemLine) const {
        juce::XmlElement root("RingsIntoCloudsPreset");
        root.setAttribute("name", name);
        root.setAttribute("category", category);
        root.setAttribute("poem", poemLine.isEmpty() ? "Custom patch crafted in Kings into Clowns." : poemLine);

        auto* paramsXml = root.createNewChildElement("Parameters");
        auto state = apvts_.copyState();
        for (int i = 0; i < state.getNumChildren(); ++i) {
            auto child = state.getChild(i);
            if (child.hasType("PARAM")) {
                auto* pNode = paramsXml->createNewChildElement("Param");
                pNode->setAttribute("id", child.getProperty("id").toString());
                pNode->setAttribute("val", static_cast<double>(child.getProperty("value")));
            }
        }

        auto* modXml = root.createNewChildElement("ModMatrix");
        saveModMatrixToXml(modXml);

        return root.writeTo(file);
    }

    bool importPresetFromFile(const juce::File& file) {
        if (!file.existsAsFile()) return false;
        const juce::File dest = userPresetDir_.getChildFile(file.getFileName());
        if (file != dest) {
            file.copyFileTo(dest);
        }
        rescanPresets();
        for (size_t i = 0; i < presets_.size(); ++i) {
            if (presets_[i].userFile == dest) {
                loadPreset(static_cast<int>(i));
                return true;
            }
        }
        return false;
    }

    bool deleteCurrentUserPreset() {
        const auto* p = getCurrentPreset();
        if (!p || p->isFactory || !p->userFile.existsAsFile()) return false;
        p->userFile.deleteFile();
        rescanPresets();
        loadPreset(std::max(0, currentIndex_ - 1));
        return true;
    }

    void saveModMatrixToXml(juce::XmlElement* modXml) const {
        if (!modXml) return;
        const auto slots = modMatrix_.getSlots();
        for (size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i].enabled && std::abs(slots[i].amount) < 0.0001f) continue;
            auto* sNode = modXml->createNewChildElement("Slot");
            sNode->setAttribute("index", static_cast<int>(i));
            sNode->setAttribute("src", static_cast<int>(slots[i].source));
            sNode->setAttribute("dst", static_cast<int>(slots[i].dest));
            sNode->setAttribute("amt", static_cast<double>(slots[i].amount));
            sNode->setAttribute("en", slots[i].enabled);
        }
    }

    void loadModMatrixFromXml(const juce::XmlElement* modXml) {
        if (!modXml) return;
        modMatrix_.clearAll();
        for (auto* sNode : modXml->getChildWithTagNameIterator("Slot")) {
            const int idx = sNode->getIntAttribute("index", -1);
            if (idx >= 0 && idx < static_cast<int>(ric::mod::ModMatrix::kMaxSlots)) {
                const auto src = static_cast<ric::mod::Source>(std::clamp(sNode->getIntAttribute("src", 0), 0, static_cast<int>(ric::mod::Source::Count) - 1));
                const auto dst = static_cast<ric::mod::Dest>(std::clamp(sNode->getIntAttribute("dst", 0), 0, static_cast<int>(ric::mod::Dest::Count) - 1));
                const float amt = static_cast<float>(sNode->getDoubleAttribute("amt", 0.0));
                const bool en = sNode->getBoolAttribute("en", true);
                modMatrix_.setSlot(static_cast<size_t>(idx), src, dst, amt, en);
            }
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts_;
    ric::mod::ModMatrix& modMatrix_;
    juce::UndoManager& undoManager_;
    juce::File userPresetDir_;
    std::vector<ric::presets::FactoryPreset> presets_;
    juce::StringArray categories_;
    int currentIndex_ = 0;
};

} // namespace ric::core
