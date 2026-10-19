#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PoeticLookAndFeel.h"
#include <array>
#include <memory>

namespace ric::ui {

class AboutOverlay : public juce::Component {
public:
    AboutOverlay() {
        setWantsKeyboardFocus(true);

        const juce::String urls[] = {
            "https://mtyas.com",
            "https://github.com/mtyas",
            "https://ko-fi.com/mtyas",
            "https://github.com/pichenettes/eurorack"
        };
        const juce::String labels[] = {
            "mtyas.com",
            "github.com/mtyas",
            "ko-fi.com/mtyas",
            "github.com/pichenettes/eurorack"
        };

        for (size_t i = 0; i < links_.size(); ++i) {
            links_[i] = std::make_unique<juce::HyperlinkButton>(labels[i], juce::URL(urls[i]));
            links_[i]->setFont(Fonts::serif(14.0f, true), false, juce::Justification::centred);
            links_[i]->setColour(juce::HyperlinkButton::textColourId, Palette::kGold);
            links_[i]->setTooltip(urls[i]);
            addAndMakeVisible(*links_[i]);
        }

        closeBtn_.setButtonText("Close");
        closeBtn_.onClick = [this]() { setVisible(false); };
        addAndMakeVisible(closeBtn_);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xFF05060C).withAlpha(0.72f));
        const auto card = cardBounds().toFloat();
        Draw::glow(g, card.getCentre().translated(0.0f, -card.getHeight() * 0.3f), card.getWidth() * 0.6f, Palette::kEmber, 0.10f);
        Draw::card(g, card, 18.0f);

        // A small emblem: a ring dissolving into a cloud of dots
        const auto em = juce::Point<float>(card.getCentreX(), card.getY() + 54.0f);
        g.setColour(Palette::kJade.withAlpha(0.9f));
        g.drawEllipse(em.x - 34.0f, em.y - 16.0f, 32.0f, 32.0f, 1.4f);
        Draw::glow(g, { em.x - 18.0f, em.y }, 22.0f, Palette::kJade, 0.35f);
        juce::Random rng(7);
        for (int i = 0; i < 22; ++i) {
            const float t = static_cast<float>(i) / 22.0f;
            const float x = em.x + 4.0f + t * 40.0f;
            const float y = em.y + (rng.nextFloat() - 0.5f) * 30.0f * (0.4f + t);
            const float s = 1.0f + rng.nextFloat() * 2.2f * (1.0f - t * 0.5f);
            g.setColour(Palette::kPearl.interpolatedWith(Palette::kIris, t).withAlpha(0.9f - t * 0.6f));
            g.fillEllipse(x - s * 0.5f, y - s * 0.5f, s, s);
        }

        auto area = cardBounds().reduced(36, 0).withTrimmedTop(84);
        g.setColour(Palette::kMoon);
        g.setFont(Fonts::serif(30.0f, true));
        g.drawText("Kings into Clowns", area.removeFromTop(38), juce::Justification::centred);

        g.setColour(Palette::kMist);
        g.setFont(Fonts::serif(14.0f, true));
        g.drawText("a breath, a body, and the sky it dissolves into", area.removeFromTop(22), juce::Justification::centred);

        g.setFont(Fonts::caps(9.5f));
        g.setColour(Palette::kFaint);
        g.drawText("BY MATTHEW TYAS (MTYAS)   -   VERSION " JucePlugin_VersionString, area.removeFromTop(26), juce::Justification::centred);

        area.removeFromTop(78); // links

        g.setColour(Palette::kEdgeSoft);
        g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX() + 40), static_cast<float>(area.getRight() - 40));
        area.removeFromTop(14);

        const juce::String notice =
            "DSP algorithms inspired by and adapted from the open-source Mutable Instruments Eurorack modules "
            "(Braids, Plaits, Rings, Clouds, Marbles) and the Oliverb alternative firmware.\n"
            "Copyright (c) 2012-2020 Emilie Gillet. Released under the MIT License: permission is granted, free of charge, "
            "to use, copy, modify, merge, publish and distribute the Software, provided the copyright notice and this "
            "permission notice are included. THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND.\n"
            "This plugin is an independent work by mtyas and is not affiliated with or endorsed by Mutable Instruments.";
        g.setColour(Palette::kMist.withAlpha(0.8f));
        g.setFont(Fonts::sans(11.0f));
        g.drawFittedText(notice, area.withTrimmedBottom(54), juce::Justification::centredTop, 10, 1.0f);
    }

    void resized() override {
        const auto card = cardBounds();
        const int y0 = card.getY() + 84 + 38 + 22 + 26 + 4;
        const int w = (card.getWidth() - 80) / 2;
        for (size_t i = 0; i < links_.size(); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            links_[i]->setBounds(card.getX() + 40 + col * w, y0 + row * 32, w, 28);
        }
        closeBtn_.setBounds(card.getCentreX() - 50, card.getBottom() - 46, 100, 28);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!cardBounds().contains(e.getPosition())) setVisible(false);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) { setVisible(false); return true; }
        return false;
    }

private:
    juce::Rectangle<int> cardBounds() const {
        return juce::Rectangle<int>(600, 480).withCentre(getLocalBounds().getCentre());
    }

    std::array<std::unique_ptr<juce::HyperlinkButton>, 4> links_;
    juce::TextButton closeBtn_;
};

} // namespace ric::ui
