#include "PoeticLookAndFeel.h"
#include <cmath>

namespace ric::ui {

//==============================================================================
namespace Fonts {

static juce::String sansName() {
#if JUCE_WINDOWS
    return "Segoe UI";
#elif JUCE_MAC
    return "Helvetica Neue";
#else
    return juce::Font::getDefaultSansSerifFontName();
#endif
}

juce::Font serif(float height, bool italic) {
    return juce::Font(juce::FontOptions("Georgia", height, italic ? juce::Font::italic : juce::Font::plain));
}

juce::Font sans(float height, bool bold) {
    return juce::Font(juce::FontOptions(sansName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font caps(float height, bool bold) {
    return juce::Font(juce::FontOptions(sansName(), height, bold ? juce::Font::bold : juce::Font::plain)
                          .withKerningFactor(0.14f));
}

} // namespace Fonts

//==============================================================================
namespace Draw {

// One pre-rendered halo, tinted on the fly: much cheaper than a radial gradient per call.
// It is a plain software image owned by a shared resource that the look-and-feel keeps alive,
// so it is released when the last editor closes. (A function-static native image used to be
// destroyed during DLL unload, under the loader lock, which hung hosts on exit.)
GlowSprite::GlowSprite() {
    constexpr int kSize = 128;
    image = juce::Image(juce::Image::SingleChannel, kSize, kSize, true, juce::SoftwareImageType());
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    const float c = (kSize - 1) * 0.5f;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) / c;
            // bright core, long soft falloff (matches the old 3-stop gradient)
            const float v = d >= 1.0f ? 0.0f
                          : (d < 0.35f ? 1.0f - (1.0f - 0.45f) * (d / 0.35f)
                                       : 0.45f * std::pow(1.0f - (d - 0.35f) / 0.65f, 1.6f));
            data.setPixelColour(x, y, juce::Colours::white.withAlpha(v));
        }
    }
}

void glow(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float intensity) {
    if (radius <= 0.5f || intensity <= 0.001f) return;
    juce::Graphics::ScopedSaveState state(g);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.setColour(colour.withMultipliedAlpha(std::clamp(intensity, 0.0f, 1.0f)));
    const juce::SharedResourcePointer<GlowSprite> sprite;
    g.drawImage(sprite->image, juce::Rectangle<float>(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f),
                juce::RectanglePlacement::stretchToFit, true);
}

void card(juce::Graphics& g, juce::Rectangle<float> r, float corner, float alpha) {
    g.setColour(juce::Colours::black.withAlpha(0.28f * alpha));
    g.fillRoundedRectangle(r.translated(0.0f, 3.0f).expanded(1.0f), corner + 1.0f);

    juce::ColourGradient body(Palette::kPanel.brighter(0.06f).withMultipliedAlpha(alpha), r.getX(), r.getY(),
                              Palette::kPanel.darker(0.18f).withMultipliedAlpha(alpha), r.getX(), r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, corner);

    g.setColour(Palette::kEdgeSoft.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);

    // Faint sheen along the top edge
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.06f * alpha), r.getX(), r.getY(),
                               juce::Colours::white.withAlpha(0.0f), r.getX(), r.getY() + 36.0f, false);
    g.setGradientFill(sheen);
    g.fillRoundedRectangle(r.withHeight(std::min(36.0f, r.getHeight())), corner);
}

void dashedLine(juce::Graphics& g, juce::Line<float> line, float dash, float gap, float thickness) {
    const float pattern[2] = { dash, gap };
    g.drawDashedLine(line, pattern, 2, thickness);
}

} // namespace Draw

//==============================================================================
PoeticLookAndFeel::PoeticLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId,     Palette::kNight);
    setColour(juce::ComboBox::backgroundColourId,            Palette::kWell);
    setColour(juce::ComboBox::textColourId,                  Palette::kMoon);
    setColour(juce::ComboBox::outlineColourId,               Palette::kEdge);
    setColour(juce::ComboBox::arrowColourId,                 Palette::kMist);
    setColour(juce::PopupMenu::backgroundColourId,           Palette::kPanel);
    setColour(juce::PopupMenu::textColourId,                 Palette::kMoon);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Palette::kPanelHi);
    setColour(juce::PopupMenu::highlightedTextColourId,      Palette::kMoon);
    setColour(juce::TextButton::buttonColourId,              Palette::kPanelHi);
    setColour(juce::TextButton::buttonOnColourId,            Palette::kJade);
    setColour(juce::TextButton::textColourOffId,             Palette::kMist);
    setColour(juce::TextButton::textColourOnId,              Palette::kMoon);
    setColour(juce::Label::textColourId,                     Palette::kMoon);
    setColour(juce::TooltipWindow::textColourId,             Palette::kMoon);
    setColour(juce::TooltipWindow::backgroundColourId,       Palette::kPanel);
    setColour(juce::TooltipWindow::outlineColourId,          Palette::kEdge);

    setColour(juce::AlertWindow::backgroundColourId,         Palette::kPanel);
    setColour(juce::AlertWindow::textColourId,               Palette::kMoon);
    setColour(juce::AlertWindow::outlineColourId,            Palette::kEdge);
    setColour(juce::TextEditor::backgroundColourId,          Palette::kWell);
    setColour(juce::TextEditor::textColourId,                Palette::kMoon);
    setColour(juce::TextEditor::outlineColourId,             Palette::kEdge);
    setColour(juce::TextEditor::focusedOutlineColourId,      Palette::kGold.withAlpha(0.6f));
    setColour(juce::TextEditor::highlightColourId,           Palette::kGold.withAlpha(0.25f));
    setColour(juce::CaretComponent::caretColourId,           Palette::kGold);
    setColour(juce::ListBox::backgroundColourId,             juce::Colours::transparentBlack);
    setColour(juce::ListBox::outlineColourId,                juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId,                Palette::kMist.withAlpha(0.3f));
    setColour(juce::HyperlinkButton::textColourId,           Palette::kGold);
}

//==============================================================================
void PoeticLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider) {
    const auto& props = slider.getProperties();
    const int tier = props.getWithDefault("knobTier", 1); // 2 = hero, 1 = medium, 0 = trim
    const bool bipolar = props.getWithDefault("bipolar", false);
    const bool dimmed = props.getWithDefault("dimmed", false);
    const bool isAssignTarget = props.getWithDefault("isAssignTarget", false);
    const bool isMidiLearning = props.getWithDefault("isMidiLearning", false);
    const bool dropHover = props.getWithDefault("dropHover", false);
    const bool inspected = props.getWithDefault("isInspected", false);
    const bool hovered = slider.isMouseOverOrDragging() || dropHover;

    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(width), static_cast<float>(height)).reduced(2.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float span = rotaryEndAngle - rotaryStartAngle;
    const float angle = rotaryStartAngle + sliderPos * span;
    const auto accent = dimmed ? Palette::kAsh : slider.findColour(juce::Slider::rotarySliderFillColourId);

    const float trackR = radius * 0.86f;
    const float stroke = tier == 2 ? 3.0f : (tier == 1 ? 2.5f : 2.0f);
    const juce::PathStrokeType arcStroke(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    auto arcPath = [&](float r, float a0, float a1) {
        juce::Path p;
        p.addCentredArc(centre.x, centre.y, r, r, 0.0f, std::min(a0, a1), std::max(a0, a1), true);
        return p;
    };
    auto pointOn = [&](float r, float a) {
        return juce::Point<float>(centre.x + std::sin(a) * r, centre.y - std::cos(a) * r);
    };

    // 1. A quiet halo whose warmth follows the value
    if (tier == 2 && !dimmed)
        Draw::glow(g, centre, radius * 1.02f, accent, 0.06f + 0.16f * sliderPos);

    // 2. Track & sundial ticks
    g.setColour(Palette::kEdge);
    g.strokePath(arcPath(trackR, rotaryStartAngle, rotaryEndAngle), arcStroke);
    if (tier >= 1) {
        const int ticks = tier == 2 ? 13 : 9;
        g.setColour(Palette::kFaint.withAlpha(0.55f));
        for (int i = 0; i < ticks; ++i) {
            const auto p = pointOn(radius * 0.99f, rotaryStartAngle + span * static_cast<float>(i) / static_cast<float>(ticks - 1));
            g.fillEllipse(p.x - 0.8f, p.y - 0.8f, 1.6f, 1.6f);
        }
    }

    // 3. Value arc, luminous
    const float arcFrom = bipolar ? rotaryStartAngle + span * 0.5f : rotaryStartAngle;
    if (std::abs(angle - arcFrom) > 0.01f) {
        const auto valuePath = arcPath(trackR, arcFrom, angle);
        g.setColour(accent.withAlpha(0.16f));
        g.strokePath(valuePath, juce::PathStrokeType(stroke * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(valuePath, arcStroke);
    }

    // 4. Live modulation: a golden orbit and a moon riding it
    if (props.contains("modValue") && !dimmed) {
        const float modNorm = std::clamp(static_cast<float>(props["modValue"]), 0.0f, 1.0f);
        const float modAngle = rotaryStartAngle + modNorm * span;
        if (std::abs(modAngle - angle) > 0.012f) {
            g.setColour(Palette::kGold.withAlpha(0.85f));
            g.strokePath(arcPath(trackR, angle, modAngle),
                         juce::PathStrokeType(stroke * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto moon = pointOn(trackR, modAngle);
        Draw::glow(g, moon, 7.0f, Palette::kGold, 0.55f);
        g.setColour(Palette::kGold);
        g.fillEllipse(moon.x - 2.4f, moon.y - 2.4f, 4.8f, 4.8f);
    }

    // 5. Modulation assignment: the selected tide's depth as an inner rose arc
    if (isAssignTarget && !dimmed) {
        const float pulse = props.getWithDefault("assignPulse", 0.5f);
        Draw::glow(g, centre, radius, Palette::kRose, 0.18f + 0.22f * pulse + (hovered ? 0.2f : 0.0f));
        g.setColour(Palette::kRose.withAlpha(0.45f + 0.45f * pulse));
        g.drawEllipse(centre.x - radius * 0.985f, centre.y - radius * 0.985f, radius * 1.97f, radius * 1.97f, 1.6f);
        const float innerR = radius * 0.71f;
        g.setColour(Palette::kRose.withAlpha(hovered ? 0.8f : 0.5f));
        g.drawEllipse(centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.2f);
        const float depth = props.getWithDefault("assignDepth", 0.0f);
        if (std::abs(depth) > 0.004f) {
            const float targetAngle = rotaryStartAngle + std::clamp(sliderPos + depth, 0.0f, 1.0f) * span;
            g.setColour(Palette::kRose);
            g.strokePath(arcPath(innerR, angle, targetAngle),
                         juce::PathStrokeType(stroke + 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // 6. The cap: a small dark stone, lit from above
    const float bodyR = radius * (isAssignTarget ? 0.58f : 0.62f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR + 2.5f, bodyR * 2.0f, bodyR * 2.0f);
    juce::ColourGradient cap(Palette::kPanelHi.brighter(hovered ? 0.22f : 0.12f), centre.x - bodyR * 0.5f, centre.y - bodyR,
                             Palette::kWell, centre.x + bodyR * 0.4f, centre.y + bodyR, false);
    g.setGradientFill(cap);
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    juce::Colour rim = Palette::kEdge;
    float rimWidth = 1.0f;
    if (isMidiLearning)      { rim = Palette::kRose; rimWidth = 2.0f; }
    else if (dropHover)      { rim = Palette::kRose; rimWidth = 1.8f; }
    else if (inspected)      { rim = Palette::kGold.withAlpha(0.85f); rimWidth = 1.6f; }
    else if (hovered)        { rim = accent.withAlpha(0.7f); }
    g.setColour(rim);
    g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, rimWidth);

    // 7. Pointer: a fine needle ending in a spark
    const auto tip = pointOn(bodyR * 0.78f, angle);
    g.setColour(Palette::kMoon.withAlpha(dimmed ? 0.4f : 0.9f));
    g.drawLine(juce::Line<float>(pointOn(bodyR * 0.22f, angle), tip), tier == 2 ? 1.8f : 1.5f);
    if (!dimmed) Draw::glow(g, tip, 5.0f, accent, 0.7f);
}

//==============================================================================
void PoeticLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                     int, int, int, int, juce::ComboBox& box) {
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
    const auto accent = box.findColour(juce::ComboBox::focusedOutlineColourId, true);
    const bool hot = box.isPopupActive() || box.isMouseOver(true);

    g.setColour(Palette::kWell.withAlpha(0.75f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(hot ? accent.withAlpha(0.7f) : Palette::kEdge);
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

    juce::Path chevron;
    const float cx = static_cast<float>(width) - 14.0f;
    const float cy = static_cast<float>(height) * 0.5f;
    chevron.startNewSubPath(cx - 3.5f, cy - 1.5f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 3.5f, cy - 1.5f);
    g.setColour(hot ? accent : Palette::kMist);
    g.strokePath(chevron, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font PoeticLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return Fonts::sans(std::min(13.0f, static_cast<float>(box.getHeight()) * 0.52f));
}

void PoeticLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

//==============================================================================
void PoeticLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.fillAll(Palette::kPanel);
    g.setColour(Palette::kEdge);
    g.drawRect(r, 1.0f);
}

void PoeticLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted,
                                          bool isTicked, bool hasSubMenu, const juce::String& text,
                                          const juce::String& /*shortcutKeyText*/, const juce::Drawable* /*icon*/,
                                          const juce::Colour* /*textColour*/) {
    if (isSeparator) {
        g.setColour(Palette::kEdgeSoft);
        g.fillRect(area.reduced(10, 0).withSizeKeepingCentre(area.getWidth() - 20, 1));
        return;
    }

    auto r = area.reduced(3, 1);
    if (isHighlighted && isActive) {
        g.setColour(Palette::kPanelHi);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(Palette::kGold.withAlpha(0.8f));
        g.fillRoundedRectangle(static_cast<float>(r.getX()) + 2.0f, static_cast<float>(r.getY()) + 6.0f,
                               2.0f, static_cast<float>(r.getHeight()) - 12.0f, 1.0f);
    }

    if (isTicked) {
        const auto c = juce::Point<float>(static_cast<float>(r.getX()) + 14.0f, static_cast<float>(r.getCentreY()));
        Draw::glow(g, c, 7.0f, Palette::kGold, 0.6f);
        g.setColour(Palette::kGold);
        g.fillEllipse(c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
    }

    g.setColour(!isActive ? Palette::kFaint : (isTicked ? Palette::kGold : Palette::kMoon));
    g.setFont(getPopupMenuFont());
    g.drawFittedText(text, r.withTrimmedLeft(26).withTrimmedRight(hasSubMenu ? 22 : 8), juce::Justification::centredLeft, 1);

    if (hasSubMenu) {
        juce::Path arrow;
        const float ax = static_cast<float>(r.getRight()) - 12.0f;
        const float ay = static_cast<float>(r.getCentreY());
        arrow.startNewSubPath(ax - 2.0f, ay - 3.5f);
        arrow.lineTo(ax + 1.5f, ay);
        arrow.lineTo(ax - 2.0f, ay + 3.5f);
        g.setColour(Palette::kMist);
        g.strokePath(arrow, juce::PathStrokeType(1.3f));
    }
}

void PoeticLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                   const juce::String& sectionName) {
    g.setFont(Fonts::serif(13.0f, true));
    g.setColour(Palette::kGold.withAlpha(0.85f));
    g.drawFittedText(sectionName, area.reduced(12, 0), juce::Justification::bottomLeft, 1);
}

juce::Font PoeticLookAndFeel::getPopupMenuFont() {
    return Fonts::sans(13.5f);
}

//==============================================================================
void PoeticLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& /*backgroundColour*/,
                                             bool isHighlighted, bool isDown) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float corner = bounds.getHeight() * 0.5f;
    const bool on = button.getToggleState();
    const auto accent = button.findColour(juce::TextButton::buttonOnColourId);

    if (on) {
        g.setColour(accent.withAlpha(isDown ? 0.32f : 0.20f));
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(accent.withAlpha(0.85f));
        g.drawRoundedRectangle(bounds, corner, 1.2f);
    } else {
        g.setColour(juce::Colours::white.withAlpha(isDown ? 0.12f : (isHighlighted ? 0.075f : 0.035f)));
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(isHighlighted ? accent.withAlpha(0.55f) : Palette::kEdge);
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }
}

void PoeticLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                       bool isHighlighted, bool /*isDown*/) {
    const bool on = button.getToggleState();
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(on ? Palette::kMoon : (isHighlighted ? Palette::kMoon : Palette::kMist));
    if (!button.isEnabled()) g.setColour(Palette::kFaint.withAlpha(0.6f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(8, 0), juce::Justification::centred, 1);
}

juce::Font PoeticLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return Fonts::caps(std::min(11.5f, static_cast<float>(buttonHeight) * 0.46f), true);
}

//==============================================================================
void PoeticLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(Palette::kPanel);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(Palette::kGold.withAlpha(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(Palette::kMoon);
    g.setFont(Fonts::serif(13.5f, true));
    g.drawFittedText(text, r.reduced(12.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> PoeticLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea) {
    const auto font = Fonts::serif(13.5f, true);
    const int textW = juce::GlyphArrangement::getStringWidthInt(font, tipText);
    const int w = std::min(340, textW + 28);
    const int lines = std::max(1, (textW + 27) / 316 + 1);
    const int h = 14 + lines * 18;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 18,
                                w, h).constrainedWithin(parentArea);
}

//==============================================================================
void PoeticLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                      bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                      bool isMouseOver, bool isMouseDown) {
    juce::Rectangle<float> thumb = isScrollbarVertical
        ? juce::Rectangle<float>(static_cast<float>(x) + static_cast<float>(width) * 0.35f, static_cast<float>(thumbStartPosition),
                                 static_cast<float>(width) * 0.3f, static_cast<float>(thumbSize))
        : juce::Rectangle<float>(static_cast<float>(thumbStartPosition), static_cast<float>(y) + static_cast<float>(height) * 0.35f,
                                 static_cast<float>(thumbSize), static_cast<float>(height) * 0.3f);
    g.setColour(Palette::kMist.withAlpha(isMouseDown ? 0.55f : (isMouseOver ? 0.4f : 0.22f)));
    g.fillRoundedRectangle(thumb, std::min(thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

void PoeticLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&) {
    g.setColour(Palette::kWell.withAlpha(0.85f));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 8.0f);
}

void PoeticLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) {
    g.setColour(editor.hasKeyboardFocus(true) ? Palette::kGold.withAlpha(0.55f) : Palette::kEdge);
    g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f, 8.0f, 1.0f);
}

} // namespace ric::ui
