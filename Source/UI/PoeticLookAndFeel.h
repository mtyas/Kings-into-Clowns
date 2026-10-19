#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ric::ui {

// Twilight palette: a night sky, with one warm-to-cool colour per stage of the journey
namespace Palette {
    inline const juce::Colour kNight     { 0xFF0A0C18 }; // deepest background
    inline const juce::Colour kDusk      { 0xFF141629 }; // chassis
    inline const juce::Colour kPanel     { 0xFF171A2D }; // stage cards
    inline const juce::Colour kPanelHi   { 0xFF22263F }; // raised surfaces / knob caps
    inline const juce::Colour kWell      { 0xFF0E1020 }; // recessed wells
    inline const juce::Colour kEdge      { 0x2EFFFFFF }; // hairline borders
    inline const juce::Colour kEdgeSoft  { 0x17FFFFFF };

    inline const juce::Colour kMoon      { 0xFFF1EBDD }; // primary text
    inline const juce::Colour kMist      { 0xFFB4B1C8 }; // secondary text
    inline const juce::Colour kFaint     { 0xFF6E6C88 }; // captions
    inline const juce::Colour kAsh       { 0xFF4A4960 }; // bypassed

    inline const juce::Colour kEmber     { 0xFFF3A66F }; // I.  the breath (voice / exciter)
    inline const juce::Colour kCoral     { 0xFFE9765F };
    inline const juce::Colour kJade      { 0xFF6FD6BB }; // II. the body (resonator)
    inline const juce::Colour kPearl     { 0xFF9EC6FF }; // III. the sky (clouds)
    inline const juce::Colour kIris      { 0xFFB9A4FF }; //      space / reverb
    inline const juce::Colour kRose      { 0xFFF28DAB }; // tides (modulation) & loom
    inline const juce::Colour kGold      { 0xFFF6D98F }; // live modulation, highlights
}

namespace Fonts {
    juce::Font serif(float height, bool italic = false);   // the poetic voice: titles, values, poems
    juce::Font sans(float height, bool bold = false);      // UI text
    juce::Font caps(float height, bool bold = false);      // spaced capitals for labels
}

namespace Draw {
    struct GlowSprite {
        GlowSprite();
        juce::Image image;
    };

    // Soft radial halo, the basic "light" of the interface
    void glow(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float intensity = 1.0f);
    // Rounded glass card with a faint top sheen
    void card(juce::Graphics& g, juce::Rectangle<float> r, float corner = 14.0f, float alpha = 1.0f);
    // Dashed line helper
    void dashedLine(juce::Graphics& g, juce::Line<float> line, float dash, float gap, float thickness);
}

class PoeticLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PoeticLookAndFeel();
    ~PoeticLookAndFeel() override = default;

private:
    // Keeps the shared glow sprite alive exactly as long as an editor exists
    juce::SharedResourcePointer<Draw::GlowSprite> glowSprite_;

public:

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 6; }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
};

} // namespace ric::ui
