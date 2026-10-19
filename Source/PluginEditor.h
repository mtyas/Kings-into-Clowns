#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/PoeticLookAndFeel.h"
#include <memory>

namespace ric::ui { class MainView; }

// The editor is a scalable window onto a fixed 1280 x 800 design surface (MainView)
class KingsIntoClownsEditor : public juce::AudioProcessorEditor {
public:
    static constexpr int kDesignWidth = 1280;
    static constexpr int kDesignHeight = 800;

    explicit KingsIntoClownsEditor(KingsIntoClownsProcessor&);
    ~KingsIntoClownsEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    KingsIntoClownsProcessor& processor_;
    ric::ui::PoeticLookAndFeel lookAndFeel_;
    std::unique_ptr<ric::ui::MainView> view_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KingsIntoClownsEditor)
};
