#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PoeticLookAndFeel.h"
#include <functional>

namespace ric::ui {

// Stage on/off. Bound to a *bypass* parameter: lit (stage sounding) when the toggle is off.
class StagePowerButton : public juce::ToggleButton {
public:
    explicit StagePowerButton(juce::Colour accent) : accent_(accent) {
        setTooltip("Let this stage sound, or pass the signal around it");
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool /*isDown*/) override {
        const bool sounding = !getToggleState();
        const auto r = getLocalBounds().toFloat().reduced(3.0f);
        const auto c = r.getCentre();
        const float rad = std::min(r.getWidth(), r.getHeight()) * 0.5f;
        const auto col = sounding ? accent_ : Palette::kAsh;

        if (sounding) Draw::glow(g, c, rad * 1.8f, accent_, isHighlighted ? 0.55f : 0.35f);
        g.setColour(Palette::kWell);
        g.fillEllipse(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour(col.withAlpha(isHighlighted ? 1.0f : 0.8f));
        g.drawEllipse(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.2f);

        juce::Path arc;
        const float ir = rad * 0.5f;
        arc.addCentredArc(c.x, c.y, ir, ir, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        g.strokePath(arc, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine(c.x, c.y - ir * 1.25f, c.x, c.y - ir * 0.2f, 1.4f);
    }

private:
    juce::Colour accent_;
};

// Small drawn icon button (chevrons, undo / redo, info)
class IconButton : public juce::Button {
public:
    enum class Icon { Left, Right, Undo, Redo, Info };

    IconButton(Icon icon, const juce::String& tooltip) : juce::Button(tooltip), icon_(icon) {
        setTooltip(tooltip);
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colours::white.withAlpha(isDown ? 0.12f : (isHighlighted ? 0.07f : 0.0f)));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);

        const auto c = r.getCentre();
        const float s = std::min(r.getWidth(), r.getHeight()) * 0.22f;
        g.setColour(isEnabled() ? (isHighlighted ? Palette::kMoon : Palette::kMist) : Palette::kAsh);
        juce::Path p;
        const juce::PathStrokeType stroke(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        switch (icon_) {
            case Icon::Left:
                p.startNewSubPath(c.x + s * 0.5f, c.y - s);
                p.lineTo(c.x - s * 0.5f, c.y);
                p.lineTo(c.x + s * 0.5f, c.y + s);
                g.strokePath(p, stroke);
                break;
            case Icon::Right:
                p.startNewSubPath(c.x - s * 0.5f, c.y - s);
                p.lineTo(c.x + s * 0.5f, c.y);
                p.lineTo(c.x - s * 0.5f, c.y + s);
                g.strokePath(p, stroke);
                break;
            case Icon::Undo:
            case Icon::Redo: {
                const float dir = icon_ == Icon::Undo ? 1.0f : -1.0f;
                p.addCentredArc(c.x, c.y + s * 0.2f, s * 1.1f, s * 0.9f, 0.0f,
                                dir > 0 ? -1.9f : 1.9f, dir > 0 ? 1.4f : -1.4f, true);
                g.strokePath(p, stroke);
                const juce::Point<float> tip(c.x - dir * s * 1.05f, c.y - s * 0.45f);
                juce::Path head;
                head.startNewSubPath(tip.x + dir * s * 0.1f, tip.y - s * 0.75f);
                head.lineTo(tip);
                head.lineTo(tip.x + dir * s * 0.8f, tip.y + s * 0.1f);
                g.strokePath(head, stroke);
                break;
            }
            case Icon::Info:
                g.drawEllipse(c.x - s * 1.5f, c.y - s * 1.5f, s * 3.0f, s * 3.0f, 1.2f);
                g.fillEllipse(c.x - 1.1f, c.y - s * 0.95f, 2.2f, 2.2f);
                g.drawLine(c.x, c.y - s * 0.25f, c.x, c.y + s * 0.85f, 1.5f);
                break;
        }
    }

private:
    Icon icon_;
};

// The current patch: its name, and beneath it its line of verse. Click to open the library.
class PresetTitle : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<void()> onClick;

    PresetTitle() {
        setTooltip("Open the library");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setPreset(const juce::String& name, const juce::String& poem) {
        name_ = name;
        poem_ = poem;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (isMouseOver()) {
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.fillRoundedRectangle(r, 12.0f);
        }
        g.setColour(Palette::kMoon);
        g.setFont(Fonts::serif(19.0f));
        g.drawFittedText(name_, r.removeFromTop(r.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredBottom, 1);
        g.setColour(Palette::kMist.withAlpha(0.9f));
        g.setFont(Fonts::serif(12.5f, true));
        g.drawFittedText(poem_, r.toNearestInt(), juce::Justification::centredTop, 1, 0.9f);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mouseWasClicked() && onClick) onClick();
    }

private:
    juce::String name_;
    juce::String poem_;
};

} // namespace ric::ui
