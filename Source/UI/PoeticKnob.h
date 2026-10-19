#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PoeticLookAndFeel.h"
#include "../Core/MidiLearnManager.h"
#include "../DSP/Modulation/ModMatrix.h"
#include <array>
#include <functional>
#include <optional>
#include <cmath>

namespace ric::ui {

enum class KnobTier : int {
    Trim = 0,     // compact utility dial
    Medium = 1,   // secondary shaping dial
    Hero = 2      // primary dial of a stage
};

// Drag descriptions starting with this prefix carry a modulation source index ("tide:3")
inline constexpr const char* kTideDragPrefix = "tide:";

class PoeticKnob : public juce::Component,
                   public juce::DragAndDropTarget {
public:
    PoeticKnob(juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramId,
               const juce::String& labelText,
               const juce::Colour& accentColour,
               KnobTier tier = KnobTier::Medium,
               std::optional<ric::mod::Dest> modDest = std::nullopt,
               ric::mod::ModMatrix* modMatrix = nullptr,
               ric::core::MidiLearnManager* midiLearn = nullptr)
        : apvts_(apvts),
          paramId_(paramId),
          labelText_(labelText),
          accentColour_(accentColour),
          tier_(tier),
          modDest_(modDest),
          modMatrix_(modMatrix),
          midiLearn_(midiLearn) {
        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setColour(juce::Slider::rotarySliderFillColourId, accentColour_);
        slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
        slider_.setMouseDragSensitivity(tier_ == KnobTier::Hero ? 260 : 210);
        slider_.setVelocityModeParameters(0.9, 1, 0.05, true, juce::ModifierKeys::shiftModifier);
        slider_.getProperties().set("knobTier", static_cast<int>(tier_));
        slider_.onValueChange = [this]() { repaint(); };
        slider_.interceptor = [this](const juce::MouseEvent& e, int phase) { return interceptMouse(e, phase); };
        slider_.onHoverChanged = [this]() { repaint(); };
        addAndMakeVisible(slider_);

        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramId, slider_);

        if (auto* p = apvts_.getParameter(paramId_)) {
            const auto range = p->getNormalisableRange();
            bipolar_ = range.start < 0.0f && range.end > 0.0f;
            slider_.getProperties().set("bipolar", bipolar_);
            slider_.setDoubleClickReturnValue(true, static_cast<double>(range.convertFrom0to1(p->getDefaultValue())));
        }
        refreshVisualBadges();
    }

    juce::Slider& getSlider() noexcept { return slider_; }
    const juce::String& getParamId() const noexcept { return paramId_; }
    std::optional<ric::mod::Dest> getModDest() const noexcept { return modDest_; }

    void setTooltipText(const juce::String& text) { slider_.setTooltip(text); }

    void setDimmed(bool dimmed) {
        if (dimmed_ == dimmed) return;
        dimmed_ = dimmed;
        slider_.getProperties().set("dimmed", dimmed);
        slider_.repaint();
        repaint();
    }

    // The dial whose tides are listed in the routing column
    void setInspected(bool inspected) {
        slider_.getProperties().set("isInspected", inspected);
        slider_.repaint();
    }

    void setModulatedNormalizedValue(float normVal) {
        if (!modDest_.has_value()) return;
        const float base = static_cast<float>(slider_.valueToProportionOfLength(slider_.getValue()));
        const bool visible = hasRouting_ || std::abs(normVal - base) > 0.003f;
        auto& props = slider_.getProperties();
        if (!visible) {
            if (props.contains("modValue")) {
                props.remove("modValue");
                slider_.repaint();
            }
            return;
        }
        const float prev = props.getWithDefault("modValue", -1.0f);
        if (std::abs(prev - normVal) > 0.0015f) {
            props.set("modValue", normVal);
            slider_.repaint();
        }
    }

    void setActiveModAssignSource(std::optional<ric::mod::Source> src) {
        activeAssignSource_ = src;
        const bool canAssign = activeAssignSource_.has_value() && modDest_.has_value();
        slider_.getProperties().set("isAssignTarget", canAssign);
        refreshVisualBadges();
    }

    void refreshVisualBadges() {
        auto& props = slider_.getProperties();
        if (modMatrix_ && modDest_.has_value()) {
            hasRouting_ = modMatrix_->getTotalDepthForDest(*modDest_) > 0.004f;
            const float depth = activeAssignSource_.has_value()
                ? modMatrix_->getConnectionAmount(*activeAssignSource_, *modDest_)
                : 0.0f;
            props.set("assignDepth", depth);
        }

        if (midiLearn_) {
            const bool isLearningThis = midiLearn_->isLearnActive() && (midiLearn_->getLearningParamId() == paramId_);
            props.set("isMidiLearning", isLearningThis);
            mappedCC_ = midiLearn_->getMappedCCForParam(paramId_);
        }
        repaint();
        slider_.repaint();
    }

    //==========================================================================
    // Drag a tide (modulation source) onto the knob to connect it
    bool isInterestedInDragSource(const SourceDetails& details) override {
        return modDest_.has_value() && modMatrix_ != nullptr
            && details.description.toString().startsWith(kTideDragPrefix);
    }

    void itemDragEnter(const SourceDetails&) override { setDropHover(true); }
    void itemDragExit(const SourceDetails&) override { setDropHover(false); }

    void itemDropped(const SourceDetails& details) override {
        setDropHover(false);
        const int srcIdx = details.description.toString().fromFirstOccurrenceOf(kTideDragPrefix, false, false).getIntValue();
        if (srcIdx < 0 || srcIdx >= static_cast<int>(ric::mod::Source::Count)) return;
        const auto src = static_cast<ric::mod::Source>(srcIdx);
        const float existing = modMatrix_->getConnectionAmount(src, *modDest_);
        modMatrix_->assignOrUpdateConnection(src, *modDest_, std::abs(existing) > 0.004f ? existing : 0.30f);
        refreshVisualBadges();
        if (onTideDropped) onTideDropped(src);
    }

    std::function<void(ric::mod::Source)> onTideDropped;
    // Clicked: lets the tides panel show which sources move this dial
    std::function<void(ric::mod::Dest)> onFocused;

    //==========================================================================
    void resized() override {
        auto b = getLocalBounds();
        const int labelH = labelHeight();
        auto dialArea = b.withTrimmedBottom(labelH);
        const int maxDia = (tier_ == KnobTier::Hero) ? 76 : (tier_ == KnobTier::Medium ? 54 : 42);
        const int side = std::min({ dialArea.getWidth(), dialArea.getHeight(), maxDia });
        slider_.setBounds(dialArea.withSizeKeepingCentre(side, side));
    }

    void paint(juce::Graphics& g) override {
        auto labelArea = getLocalBounds().removeFromBottom(labelHeight());
        const bool hovered = slider_.isMouseOverOrDragging() || isMouseOver(true);

        // Assigning a tide: show its depth
        if (activeAssignSource_.has_value() && modDest_.has_value() && modMatrix_) {
            const float amt = modMatrix_->getConnectionAmount(*activeAssignSource_, *modDest_);
            const int pct = static_cast<int>(std::round(amt * 100.0f));
            if (pct != 0 || isDraggingModDepth_ || hovered) {
                g.setColour(Palette::kRose);
                g.setFont(Fonts::serif(tier_ == KnobTier::Hero ? 14.0f : 12.5f, true));
                g.drawFittedText((pct > 0 ? "+" : "") + juce::String(pct) + "%", labelArea, juce::Justification::centredTop, 1);
                return;
            }
        }

        // Hover / drag: the value, in the poetic voice
        if (hovered && !dimmed_) {
            g.setColour(Palette::kMoon);
            g.setFont(Fonts::serif(tier_ == KnobTier::Hero ? 14.0f : 12.5f, true));
            juce::String valStr = (apvts_.getParameter(paramId_) != nullptr)
                ? apvts_.getParameter(paramId_)->getCurrentValueAsText()
                : juce::String(slider_.getValue(), 2);
            g.drawFittedText(valStr, labelArea, juce::Justification::centredTop, 1);
            return;
        }

        g.setColour(dimmed_ ? Palette::kAsh : (tier_ == KnobTier::Hero ? Palette::kMoon.withAlpha(0.88f) : Palette::kMist));
        g.setFont(Fonts::caps(tier_ == KnobTier::Hero ? 10.0f : 9.0f, tier_ == KnobTier::Hero));
        juce::String text = labelText_.toUpperCase();
        if (mappedCC_ >= 0) text += "  CC" + juce::String(mappedCC_);
        g.drawFittedText(text, labelArea, juce::Justification::centredTop, 1);
    }

private:
    // Slider that lets the knob claim mouse gestures (assign mode, MIDI learn, context menu)
    class KnobSlider : public juce::Slider {
    public:
        std::function<bool(const juce::MouseEvent&, int)> interceptor; // phase: 0 down, 1 drag, 2 up, 3 double
        std::function<void()> onHoverChanged;

        void mouseDown(const juce::MouseEvent& e) override { if (!(interceptor && interceptor(e, 0))) Slider::mouseDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { if (!(interceptor && interceptor(e, 1))) Slider::mouseDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { if (!(interceptor && interceptor(e, 2))) Slider::mouseUp(e); }
        void mouseDoubleClick(const juce::MouseEvent& e) override { if (!(interceptor && interceptor(e, 3))) Slider::mouseDoubleClick(e); }
        void mouseEnter(const juce::MouseEvent& e) override { Slider::mouseEnter(e); if (onHoverChanged) onHoverChanged(); }
        void mouseExit(const juce::MouseEvent& e) override { Slider::mouseExit(e); if (onHoverChanged) onHoverChanged(); }
    };

    int labelHeight() const noexcept { return tier_ == KnobTier::Hero ? 20 : 17; }

    void setDropHover(bool h) {
        slider_.getProperties().set("dropHover", h);
        slider_.repaint();
    }

    bool interceptMouse(const juce::MouseEvent& e, int phase) {
        if (phase == 0 && modDest_.has_value() && onFocused) onFocused(*modDest_);
        if (phase == 0 && e.mods.isPopupMenu()) {
            showContextMenu();
            return true;
        }
        if (e.mods.isPopupMenu()) return true;

        if (midiLearn_ && midiLearn_->isLearnActive()) {
            if (phase == 0) {
                midiLearn_->setTargetParameterForLearn(paramId_);
                refreshVisualBadges();
            }
            return true;
        }

        const bool assigning = activeAssignSource_.has_value() && modDest_.has_value() && modMatrix_;
        if (!assigning) return false;

        switch (phase) {
            case 0:
                dragStartModDepth_ = modMatrix_->getConnectionAmount(*activeAssignSource_, *modDest_);
                isDraggingModDepth_ = true;
                break;
            case 1: {
                const float delta = -static_cast<float>(e.getDistanceFromDragStartY()) / 130.0f;
                modMatrix_->assignOrUpdateConnection(*activeAssignSource_, *modDest_, std::clamp(dragStartModDepth_ + delta, -1.0f, 1.0f));
                refreshVisualBadges();
                break;
            }
            case 2:
                isDraggingModDepth_ = false;
                break;
            case 3:
                modMatrix_->assignOrUpdateConnection(*activeAssignSource_, *modDest_, 0.0f);
                refreshVisualBadges();
                break;
            default: break;
        }
        repaint();
        return true;
    }

    void showContextMenu() {
        static constexpr std::array<ric::mod::Source, 7> kSources = {
            ric::mod::Source::LFO1, ric::mod::Source::LFO2, ric::mod::Source::LFO3,
            ric::mod::Source::ENV1, ric::mod::Source::ENV2, ric::mod::Source::ENV3,
            ric::mod::Source::LOOM
        };
        static constexpr std::array<float, 6> kDepths = { 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f };

        juce::PopupMenu menu;
        // Menus live on the desktop, outside the editor, so hand them our look explicitly
        menu.setLookAndFeel(&getLookAndFeel());
        menu.addSectionHeader(labelText_);
        menu.addItem(1, "Return to default");

        if (midiLearn_) {
            menu.addSeparator();
            menu.addItem(2, "Learn MIDI CC");
            if (mappedCC_ >= 0) menu.addItem(3, "Forget CC " + juce::String(mappedCC_));
        }

        if (modMatrix_ && modDest_.has_value()) {
            menu.addSeparator();
            menu.addSectionHeader("Tides");
            for (size_t i = 0; i < kSources.size(); ++i) {
                const float curAmt = modMatrix_->getConnectionAmount(kSources[i], *modDest_);
                juce::PopupMenu sub;
                const int baseId = 100 + static_cast<int>(i) * 10;
                for (size_t d = 0; d < kDepths.size(); ++d) {
                    const int pct = static_cast<int>(kDepths[d] * 100.0f);
                    sub.addItem(baseId + static_cast<int>(d) + 1, (pct > 0 ? "+" : "") + juce::String(pct) + "%",
                                true, std::abs(curAmt - kDepths[d]) < 0.005f);
                }
                sub.addSeparator();
                sub.addItem(baseId + 9, "Off", true, std::abs(curAmt) < 0.005f);

                juce::String itemLabel = ric::mod::getSourceName(kSources[i]);
                if (std::abs(curAmt) >= 0.005f)
                    itemLabel += "   " + juce::String(static_cast<int>(std::round(curAmt * 100.0f))) + "%";
                menu.addSubMenu(itemLabel, sub, true, nullptr, std::abs(curAmt) >= 0.005f);
            }
            menu.addItem(99, "Still the tides on " + labelText_);
        }

        juce::Component::SafePointer<PoeticKnob> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&slider_),
                           [safe](int result) {
                               if (safe == nullptr || result == 0) return;
                               auto& self = *safe;
                               if (result == 1) {
                                   if (auto* p = self.apvts_.getParameter(self.paramId_)) {
                                       p->beginChangeGesture();
                                       p->setValueNotifyingHost(p->getDefaultValue());
                                       p->endChangeGesture();
                                   }
                               } else if (result == 2 && self.midiLearn_) {
                                   self.midiLearn_->setTargetParameterForLearn(self.paramId_);
                               } else if (result == 3 && self.midiLearn_) {
                                   self.midiLearn_->clearMappingForParam(self.paramId_);
                               } else if (result == 99 && self.modMatrix_ && self.modDest_.has_value()) {
                                   self.modMatrix_->clearConnectionsForDest(*self.modDest_);
                               } else if (result >= 100 && self.modMatrix_ && self.modDest_.has_value()) {
                                   const int srcIdx = (result - 100) / 10;
                                   const int act = (result - 100) % 10;
                                   if (srcIdx >= 0 && srcIdx < static_cast<int>(kSources.size())) {
                                       const float depth = (act >= 1 && act <= static_cast<int>(kDepths.size()))
                                           ? kDepths[static_cast<size_t>(act - 1)] : 0.0f;
                                       self.modMatrix_->assignOrUpdateConnection(kSources[static_cast<size_t>(srcIdx)], *self.modDest_, depth);
                                   }
                               }
                               self.refreshVisualBadges();
                           });
    }

    juce::AudioProcessorValueTreeState& apvts_;
    juce::String paramId_;
    juce::String labelText_;
    juce::Colour accentColour_;
    KnobTier tier_ = KnobTier::Medium;
    std::optional<ric::mod::Dest> modDest_;
    ric::mod::ModMatrix* modMatrix_ = nullptr;
    ric::core::MidiLearnManager* midiLearn_ = nullptr;

    std::optional<ric::mod::Source> activeAssignSource_;
    bool isDraggingModDepth_ = false;
    bool hasRouting_ = false;
    bool bipolar_ = false;
    bool dimmed_ = false;
    float dragStartModDepth_ = 0.0f;
    int mappedCC_ = -1;

    KnobSlider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

} // namespace ric::ui
