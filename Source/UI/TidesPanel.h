#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PoeticLookAndFeel.h"
#include "PoeticKnob.h"
#include "../Core/LockFreeVisualFifo.h"
#include "../Core/ParameterDefinitions.h"
#include "../DSP/Modulation/ModMatrix.h"
#include <array>
#include <functional>
#include <optional>

namespace ric::ui {

namespace Tides {
    inline constexpr int kCount = static_cast<int>(ric::mod::Source::Count);
    inline constexpr int kHistory = 72;

    inline bool isLfo(int i) { return i >= 0 && i < 3; }
    inline bool isEnv(int i) { return i >= 3 && i < 6; }
    inline bool isLoom(int i) { return i == 6; }

    inline juce::Colour colour(int i) {
        if (isLfo(i)) return Palette::kGold;
        if (isEnv(i)) return Palette::kRose;
        return Palette::kIris;
    }

    inline juce::String title(int i) {
        static const char* kTitles[kCount] = { "LFO 1", "LFO 2", "LFO 3", "ENV 1", "ENV 2", "ENV 3", "LOOM" };
        return kTitles[std::clamp(i, 0, kCount - 1)];
    }

    inline juce::String poem(int i) {
        static const char* kPoems[kCount] = {
            "a slow swell", "a second current", "the undertow",
            "the shape of each note", "a passing gust", "a long exhale",
            "chance, remembered"
        };
        return kPoems[std::clamp(i, 0, kCount - 1)];
    }

    inline void drawShapeIcon(juce::Graphics& g, int shape, juce::Rectangle<float> r, float thickness) {
        juce::Path p;
        constexpr int kPts = 40;
        for (int i = 0; i <= kPts; ++i) {
            const float t = static_cast<float>(i) / kPts;
            float v = 0.0f;
            switch (shape) {
                case 0: v = std::sin(t * juce::MathConstants<float>::twoPi); break;
                case 1: v = 1.0f - 4.0f * std::abs(std::round(t - 0.25f) - (t - 0.25f)); break;
                case 2: v = 2.0f * t - 1.0f; break;
                default: {
                    static constexpr float kSteps[5] = { 0.3f, -0.7f, 0.9f, -0.2f, 0.5f };
                    v = kSteps[std::min(4, static_cast<int>(t * 5.0f))];
                    break;
                }
            }
            const juce::Point<float> pt(r.getX() + t * r.getWidth(), r.getCentreY() - v * r.getHeight() * 0.45f);
            if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
        }
        g.strokePath(p, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
// A modulation source you can select, and drag onto any glowing dial
class TideChip : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<void(int)> onSelect;

    explicit TideChip(int index) : index_(index) {
        setTooltip("Drag " + Tides::title(index) + " onto a dial to let it move with the tide");
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void setSelected(bool s) { if (selected_ != s) { selected_ = s; repaint(); } }
    void setSubtitle(const juce::String& s) { if (subtitle_ != s) { subtitle_ = s; repaint(); } }
    void setRouteCount(int n) { if (routes_ != n) { routes_ = n; repaint(); } }

    void push(float value) {
        history_[write_] = value;
        write_ = (write_ + 1) % Tides::kHistory;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const auto col = Tides::colour(index_);
        const bool hot = selected_ || isMouseOver();

        g.setColour(selected_ ? col.withAlpha(0.12f) : juce::Colours::white.withAlpha(hot ? 0.05f : 0.025f));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(selected_ ? col.withAlpha(0.8f) : (hot ? col.withAlpha(0.4f) : Palette::kEdgeSoft));
        g.drawRoundedRectangle(r, 10.0f, selected_ ? 1.3f : 1.0f);

        // Live trace of the tide's recent past
        auto trace = r.reduced(8.0f, 7.0f).removeFromRight(r.getWidth() * 0.34f);
        juce::Path p;
        const bool unipolar = Tides::isEnv(index_);
        for (int i = 0; i < Tides::kHistory; ++i) {
            const float v = history_[static_cast<size_t>((write_ + i) % Tides::kHistory)];
            const float yNorm = unipolar ? (1.0f - std::clamp(v, 0.0f, 1.0f)) : (0.5f - 0.5f * std::clamp(v, -1.0f, 1.0f));
            const juce::Point<float> pt(trace.getX() + trace.getWidth() * static_cast<float>(i) / (Tides::kHistory - 1),
                                        trace.getY() + yNorm * trace.getHeight());
            if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
        }
        g.setColour(col.withAlpha(selected_ ? 0.95f : 0.6f));
        g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved));
        const auto last = p.getCurrentPosition();
        Draw::glow(g, last, 6.0f, col, 0.8f);

        auto text = r.reduced(10.0f, 5.0f);
        g.setColour(Palette::kMoon);
        g.setFont(Fonts::caps(10.0f, true));
        g.drawText(Tides::title(index_), text.removeFromTop(text.getHeight() * 0.5f), juce::Justification::bottomLeft);
        g.setColour(Palette::kMist);
        g.setFont(Fonts::serif(11.0f, true));
        g.drawText(subtitle_, text.withWidth(r.getWidth() * 0.64f), juce::Justification::topLeft);

        if (routes_ > 0) {
            // tiny dots: how many dials this tide is moving
            for (int i = 0; i < std::min(routes_, 6); ++i) {
                g.setColour(col.withAlpha(0.9f));
                g.fillEllipse(r.getX() + 10.0f + static_cast<float>(i) * 6.0f, r.getY() + 5.0f, 3.0f, 3.0f);
            }
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { if (onSelect) onSelect(index_); }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (e.getDistanceFromDragStart() < 6) return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
            if (!container->isDragAndDropActive()) {
                juce::Image img(juce::Image::ARGB, 28, 28, true);
                {
                    juce::Graphics ig(img);
                    Draw::glow(ig, { 14.0f, 14.0f }, 14.0f, Tides::colour(index_), 1.0f);
                    ig.setColour(Palette::kMoon);
                    ig.fillEllipse(10.0f, 10.0f, 8.0f, 8.0f);
                }
                container->startDragging(juce::String(kTideDragPrefix) + juce::String(index_), this,
                                         juce::ScaledImage(img), true);
            }
        }
    }

private:
    int index_ = 0;
    bool selected_ = false;
    int routes_ = 0;
    juce::String subtitle_;
    std::array<float, Tides::kHistory> history_{};
    int write_ = 0;
};

//==============================================================================
// Drawn LFO shape picker bound to a choice parameter
class ShapePicker : public juce::Component {
public:
    ShapePicker(juce::RangedAudioParameter& param, juce::Colour colour)
        : colour_(colour),
          attachment_(param, [this](float v) { shape_ = static_cast<int>(std::round(v)); repaint(); }, nullptr) {
        attachment_.sendInitialUpdate();
    }

    void paint(juce::Graphics& g) override {
        const float w = static_cast<float>(getWidth()) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            auto cell = juce::Rectangle<float>(static_cast<float>(i) * w, 0.0f, w, static_cast<float>(getHeight())).reduced(2.0f);
            const bool on = i == shape_;
            g.setColour(on ? colour_.withAlpha(0.16f) : juce::Colours::white.withAlpha(0.03f));
            g.fillRoundedRectangle(cell, 7.0f);
            g.setColour(on ? colour_.withAlpha(0.8f) : Palette::kEdgeSoft);
            g.drawRoundedRectangle(cell, 7.0f, 1.0f);
            g.setColour(on ? colour_ : Palette::kMist);
            Tides::drawShapeIcon(g, i, cell.reduced(cell.getWidth() * 0.2f, cell.getHeight() * 0.28f), on ? 1.6f : 1.2f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const int idx = std::clamp(static_cast<int>(static_cast<float>(e.x) / (static_cast<float>(getWidth()) / 4.0f)), 0, 3);
        attachment_.setValueAsCompleteGesture(static_cast<float>(idx));
    }

private:
    juce::Colour colour_;
    int shape_ = 0;
    juce::ParameterAttachment attachment_;
};

//==============================================================================
// Scrolling picture of an LFO's real output
class TideTrace : public juce::Component {
public:
    void setColour(juce::Colour c) { colour_ = c; }
    void setHistory(const std::array<float, Tides::kHistory>& h, int write) { history_ = h; write_ = write; repaint(); }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Palette::kWell.withAlpha(0.8f));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(Palette::kEdgeSoft);
        g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
        r = r.reduced(10.0f, 10.0f);

        g.setColour(Palette::kEdgeSoft);
        g.drawHorizontalLine(static_cast<int>(r.getCentreY()), r.getX(), r.getRight());

        juce::Path p;
        for (int i = 0; i < Tides::kHistory; ++i) {
            const float v = std::clamp(history_[static_cast<size_t>((write_ + i) % Tides::kHistory)], -1.0f, 1.0f);
            const juce::Point<float> pt(r.getX() + r.getWidth() * static_cast<float>(i) / (Tides::kHistory - 1),
                                        r.getCentreY() - v * r.getHeight() * 0.5f);
            if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
        }
        juce::Path fill(p);
        fill.lineTo(r.getRight(), r.getCentreY());
        fill.lineTo(r.getX(), r.getCentreY());
        fill.closeSubPath();
        g.setColour(colour_.withAlpha(0.10f));
        g.fillPath(fill);
        g.setColour(colour_);
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved));
        const auto last = p.getCurrentPosition();
        Draw::glow(g, last, 10.0f, colour_, 0.9f);
        g.setColour(Palette::kMoon);
        g.fillEllipse(last.x - 2.0f, last.y - 2.0f, 4.0f, 4.0f);
    }

private:
    juce::Colour colour_ = Palette::kGold;
    std::array<float, Tides::kHistory> history_{};
    int write_ = 0;
};

//==============================================================================
// ADSR you can shape by hand: drag the peak (attack), the shoulder (decay & sustain), the tail (release)
class EnvelopeView : public juce::Component {
public:
    EnvelopeView(juce::AudioProcessorValueTreeState& apvts, const char* a, const char* d, const char* s, const char* r)
        : params_{ apvts.getParameter(a), apvts.getParameter(d), apvts.getParameter(s), apvts.getParameter(r) } {
        for (size_t i = 0; i < 4; ++i) {
            attachments_[i] = std::make_unique<juce::ParameterAttachment>(*params_[i], [this](float) { repaint(); }, nullptr);
        }
    }

    void setLiveLevel(float v) {
        if (std::abs(v - live_) > 0.002f) { live_ = v; repaint(); }
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Palette::kWell.withAlpha(0.8f));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(Palette::kEdgeSoft);
        g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);

        const auto pts = points();
        juce::Path p;
        p.startNewSubPath(pts[0]);
        // curved decay & release, like the engine's exponential segments
        p.lineTo(pts[1]);
        p.quadraticTo({ pts[1].x + (pts[2].x - pts[1].x) * 0.15f, pts[2].y }, pts[2]);
        p.lineTo(pts[3]);
        p.quadraticTo({ pts[3].x + (pts[4].x - pts[3].x) * 0.15f, pts[4].y }, pts[4]);

        juce::Path fill(p);
        fill.lineTo(pts[0]);
        fill.closeSubPath();
        juce::ColourGradient grad(Palette::kRose.withAlpha(0.22f), 0.0f, pts[1].y, Palette::kRose.withAlpha(0.02f), 0.0f, pts[0].y, false);
        g.setGradientFill(grad);
        g.fillPath(fill);
        g.setColour(Palette::kRose);
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Live level of the newest voice's envelope
        if (live_ > 0.002f) {
            const float y = pts[0].y - live_ * (pts[0].y - pts[1].y);
            g.setColour(Palette::kGold.withAlpha(0.35f));
            Draw::dashedLine(g, { pts[0].x, y, pts[4].x, y }, 3.0f, 4.0f, 1.0f);
        }

        for (int i = 1; i <= 4; ++i) {
            if (i == 2) continue; // the sustain tail end is not a handle
            const int handle = i == 1 ? 0 : (i == 3 ? 1 : 2);
            const auto c = pts[static_cast<size_t>(i)];
            const bool hot = hoverHandle_ == handle || dragHandle_ == handle;
            Draw::glow(g, c, hot ? 12.0f : 8.0f, Palette::kRose, hot ? 0.9f : 0.5f);
            g.setColour(Palette::kMoon);
            g.fillEllipse(c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        }

        g.setFont(Fonts::caps(8.5f));
        g.setColour(Palette::kFaint);
        g.drawText("A", juce::Rectangle<float>(pts[0].x, r.getBottom() - 14.0f, pts[1].x - pts[0].x, 12.0f), juce::Justification::centred);
        g.drawText("D", juce::Rectangle<float>(pts[1].x, r.getBottom() - 14.0f, pts[2].x - pts[1].x, 12.0f), juce::Justification::centred);
        g.drawText("S", juce::Rectangle<float>(pts[2].x, r.getBottom() - 14.0f, pts[3].x - pts[2].x, 12.0f), juce::Justification::centred);
        g.drawText("R", juce::Rectangle<float>(pts[3].x, r.getBottom() - 14.0f, pts[4].x - pts[3].x, 12.0f), juce::Justification::centred);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int h = handleAt(e.position);
        if (h != hoverHandle_) { hoverHandle_ = h; repaint(); }
        setMouseCursor(h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit(const juce::MouseEvent&) override { hoverHandle_ = -1; repaint(); }

    void mouseDown(const juce::MouseEvent& e) override {
        dragHandle_ = handleAt(e.position);
        if (dragHandle_ < 0) return;
        for (auto i : involved(dragHandle_)) attachments_[i]->beginGesture();
        startNorm_ = { norm(0), norm(1), norm(2), norm(3) };
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragHandle_ < 0) return;
        const auto inner = plotArea();
        const float dx = static_cast<float>(e.getDistanceFromDragStartX()) / (inner.getWidth() * kSegment);
        const float dy = static_cast<float>(e.getDistanceFromDragStartY()) / inner.getHeight();
        switch (dragHandle_) {
            case 0: setNorm(0, startNorm_[0] + dx); break;
            case 1: setNorm(1, startNorm_[1] + dx); setNorm(2, startNorm_[2] - dy); break;
            case 2: setNorm(3, startNorm_[3] + dx); break;
            default: break;
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (dragHandle_ >= 0)
            for (auto i : involved(dragHandle_)) attachments_[i]->endGesture();
        dragHandle_ = -1;
    }

private:
    static constexpr float kSegment = 0.27f; // share of the width each timed stage may take

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced(12.0f, 12.0f).withTrimmedBottom(6.0f); }
    float norm(size_t i) const { return params_[i]->getValue(); }

    void setNorm(size_t i, float v) {
        const auto& range = params_[i]->getNormalisableRange();
        attachments_[i]->setValueAsPartOfGesture(range.convertFrom0to1(std::clamp(v, 0.0f, 1.0f)));
    }

    static std::vector<size_t> involved(int handle) {
        if (handle == 0) return { 0 };
        if (handle == 1) return { 1, 2 };
        return { 3 };
    }

    std::array<juce::Point<float>, 5> points() const {
        const auto r = plotArea();
        const float w = r.getWidth();
        const float a = 0.02f + norm(0) * kSegment;
        const float d = 0.02f + norm(1) * kSegment;
        const float s = norm(2);
        const float rel = 0.02f + norm(3) * kSegment;
        const float hold = std::max(0.06f, 1.0f - (a + d + rel));
        const float x0 = r.getX();
        const float bottom = r.getBottom();
        const float top = r.getY();
        const float susY = bottom - s * (bottom - top);
        const float xA = x0 + a * w;
        const float xD = xA + d * w;
        const float xS = std::min(r.getRight() - rel * w, xD + hold * w);
        return { juce::Point<float>{ x0, bottom }, { xA, top }, { xD, susY }, { xS, susY }, { std::min(r.getRight(), xS + rel * w), bottom } };
    }

    int handleAt(juce::Point<float> p) const {
        const auto pts = points();
        const std::array<juce::Point<float>, 3> handles = { pts[1], pts[2], pts[4] };
        int best = -1;
        float bestD = 14.0f;
        for (int i = 0; i < 3; ++i) {
            const float dd = handles[static_cast<size_t>(i)].getDistanceFrom(p);
            if (dd < bestD) { bestD = dd; best = i; }
        }
        return best;
    }

    std::array<juce::RangedAudioParameter*, 4> params_;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 4> attachments_;
    std::array<float, 4> startNorm_{};
    int dragHandle_ = -1;
    int hoverHandle_ = -1;
    float live_ = 0.0f;
};

//==============================================================================
// The generative loom: sixteen remembered steps, the loop that is woven, the step now sounding
class LoomView : public juce::Component {
public:
    void update(const ric::core::VisualSnapshot& s) {
        snap_ = s;
        if (s.loomTrigT1) flashT1_ = 1.0f;
        if (s.loomTrigT3) flashT3_ = 1.0f;
        flashT1_ *= 0.88f;
        flashT3_ *= 0.88f;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Palette::kWell.withAlpha(0.8f));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(Palette::kEdgeSoft);
        g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
        r = r.reduced(12.0f, 12.0f);

        const bool on = snap_.loomEnabled;
        const int len = std::clamp(static_cast<int>(snap_.loomLength), 1, 16);
        const float stepW = r.getWidth() / 16.0f;
        auto pos = [&](int i) {
            const float v = std::clamp(snap_.loomStepsX1[static_cast<size_t>(i)], -1.0f, 1.0f);
            return juce::Point<float>(r.getX() + stepW * (static_cast<float>(i) + 0.5f), r.getCentreY() - v * r.getHeight() * 0.42f);
        };

        juce::Path woven;
        for (int i = 0; i < len; ++i) {
            if (i == 0) woven.startNewSubPath(pos(i)); else woven.lineTo(pos(i));
        }
        if (len > 2) woven.closeSubPath();
        g.setColour(Palette::kIris.withAlpha(on ? 0.35f : 0.12f));
        g.strokePath(woven, juce::PathStrokeType(1.0f));

        for (int i = 0; i < 16; ++i) {
            const auto p = pos(i);
            const bool inLoop = i < len;
            const bool current = on && i == static_cast<int>(snap_.loomStep);
            if (current) {
                Draw::glow(g, p, 12.0f + flashT1_ * 10.0f, Palette::kGold, 0.9f);
                if (flashT3_ > 0.05f) {
                    g.setColour(Palette::kIris.withAlpha(flashT3_));
                    const float rr = 6.0f + (1.0f - flashT3_) * 14.0f;
                    g.drawEllipse(p.x - rr, p.y - rr, rr * 2.0f, rr * 2.0f, 1.0f);
                }
            }
            const float sz = current ? 6.0f : (inLoop ? 4.0f : 2.4f);
            g.setColour(current ? Palette::kMoon : (inLoop ? Palette::kIris.withAlpha(on ? 0.9f : 0.4f) : Palette::kFaint.withAlpha(0.5f)));
            g.fillEllipse(p.x - sz * 0.5f, p.y - sz * 0.5f, sz, sz);
        }

        if (!on) {
            g.setFont(Fonts::serif(12.5f, true));
            g.setColour(Palette::kMist);
            g.drawText("the loom is resting", r, juce::Justification::centredBottom);
        }
    }

private:
    ric::core::VisualSnapshot snap_{};
    float flashT1_ = 0.0f;
    float flashT3_ = 0.0f;
};

//==============================================================================
// Where the selected tide flows: drag a row to change depth, click the cross to let it go
class RoutingList : public juce::Component {
public:
    explicit RoutingList(ric::mod::ModMatrix& m) : matrix_(m) {}

    // Two ways to read the matrix: a tide and the dials it moves, or a dial and the tides moving it
    void setSource(int src) { source_ = src; destMode_ = false; rebuild(); }
    void setDest(ric::mod::Dest d) { dest_ = d; destMode_ = true; rebuild(); }
    bool isDestMode() const noexcept { return destMode_; }
    ric::mod::Dest getDest() const noexcept { return dest_; }

    void refreshIfChanged() {
        if (matrix_.getVersion() != seenVersion_) rebuild();
    }

    int getRouteCount() const noexcept { return static_cast<int>(rows_.size()); }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (rows_.empty()) {
            g.setFont(Fonts::serif(13.0f, true));
            g.setColour(Palette::kMist);
            const juce::String text = destMode_
                ? "Nothing moves " + ric::mod::getDestName(dest_) + " yet.\nDrag a tide onto the dial\nto set it in motion."
                : juce::String("This tide moves nothing yet.\nDrag its chip onto a dial,\nor press touch to tune.");
            g.drawFittedText(text, r.reduced(8.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            const auto row = rowBounds(static_cast<int>(i));
            if (row.getBottom() > r.getBottom()) break;
            const auto& route = rows_[i];
            const auto col = Tides::colour(static_cast<int>(route.source));
            const bool hot = static_cast<int>(i) == hoverRow_ || static_cast<int>(i) == dragRow_;

            g.setColour(juce::Colours::white.withAlpha(hot ? 0.06f : 0.025f));
            g.fillRoundedRectangle(row, 6.0f);

            // depth bar from the centre
            auto bar = juce::Rectangle<float>(row.getX() + 112.0f, row.getCentreY() - 2.0f, row.getWidth() - 112.0f - 82.0f, 4.0f);
            g.setColour(Palette::kEdgeSoft);
            g.fillRoundedRectangle(bar, 2.0f);
            const float cx = bar.getCentreX();
            const float span = route.amount * bar.getWidth() * 0.5f;
            g.setColour(col.withAlpha(0.9f));
            g.fillRoundedRectangle(std::min(cx, cx + span), bar.getY(), std::max(2.0f, std::abs(span)), bar.getHeight(), 2.0f);

            g.setFont(Fonts::sans(12.0f));
            g.setColour(Palette::kMoon);
            g.drawText(destMode_ ? ric::mod::getSourceName(route.source) : ric::mod::getDestName(route.dest),
                       row.reduced(10.0f, 0.0f).withWidth(100.0f), juce::Justification::centredLeft);
            const int pct = static_cast<int>(std::round(route.amount * 100.0f));
            g.setFont(Fonts::serif(12.0f, true));
            g.setColour(col);
            g.drawText((pct > 0 ? "+" : "") + juce::String(pct) + "%",
                       juce::Rectangle<float>(row.getRight() - 74.0f, row.getY(), 44.0f, row.getHeight()), juce::Justification::centredRight);

            // let go
            const auto x = closeBounds(row);
            g.setColour(hot ? Palette::kMoon : Palette::kFaint);
            g.drawLine(x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.2f);
            g.drawLine(x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.2f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int r = rowAt(e.position);
        if (r != hoverRow_) { hoverRow_ = r; repaint(); }
    }
    void mouseExit(const juce::MouseEvent&) override { hoverRow_ = -1; repaint(); }

    void mouseDown(const juce::MouseEvent& e) override {
        const int r = rowAt(e.position);
        if (r < 0) return;
        const auto& route = rows_[static_cast<size_t>(r)];
        if (closeBounds(rowBounds(r)).expanded(5.0f).contains(e.position)) {
            matrix_.assignOrUpdateConnection(route.source, route.dest, 0.0f);
            rebuild();
            return;
        }
        dragRow_ = r;
        dragStartAmount_ = route.amount;
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragRow_ < 0 || dragRow_ >= static_cast<int>(rows_.size())) return;
        auto& route = rows_[static_cast<size_t>(dragRow_)];
        const float amt = std::clamp(dragStartAmount_ + static_cast<float>(e.getDistanceFromDragStartX()) / 120.0f, -1.0f, 1.0f);
        // keep the row alive while dragging through zero
        const float safe = std::abs(amt) < 0.006f ? (amt < 0.0f ? -0.006f : 0.006f) : amt;
        matrix_.assignOrUpdateConnection(route.source, route.dest, safe);
        route.amount = safe;
        seenVersion_ = matrix_.getVersion();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { dragRow_ = -1; }

private:
    juce::Rectangle<float> rowBounds(int i) const {
        return { 0.0f, static_cast<float>(i) * 24.0f, static_cast<float>(getWidth()), 21.0f };
    }
    static juce::Rectangle<float> closeBounds(juce::Rectangle<float> row) {
        return { row.getRight() - 20.0f, row.getCentreY() - 3.5f, 7.0f, 7.0f };
    }
    int rowAt(juce::Point<float> p) const {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rowBounds(static_cast<int>(i)).contains(p)) return static_cast<int>(i);
        return -1;
    }

    void rebuild() {
        seenVersion_ = matrix_.getVersion();
        rows_.clear();
        for (const auto& s : matrix_.getSlots()) {
            const bool match = destMode_ ? s.dest == dest_ : static_cast<int>(s.source) == source_;
            if (s.enabled && match) rows_.push_back(s);
        }
        std::sort(rows_.begin(), rows_.end(), [this](const auto& a, const auto& b) {
            return destMode_ ? a.source < b.source : a.dest < b.dest;
        });
        repaint();
    }

    ric::mod::ModMatrix& matrix_;
    int source_ = 0;
    ric::mod::Dest dest_ = ric::mod::Dest::OscTimbre;
    bool destMode_ = false;
    uint32_t seenVersion_ = 0xFFFFFFFFu;
    std::vector<ric::mod::ModSlot> rows_;
    int hoverRow_ = -1;
    int dragRow_ = -1;
    float dragStartAmount_ = 0.0f;
};

//==============================================================================
class TidesPanel : public juce::Component {
public:
    std::function<void(std::optional<ric::mod::Source>)> onAssignModeChanged;
    std::function<void()> onRoutingChanged;

    TidesPanel(juce::AudioProcessorValueTreeState& apvts,
               ric::mod::ModMatrix& matrix,
               ric::core::MidiLearnManager& midiLearn)
        : apvts_(apvts), matrix_(matrix), routing_(matrix) {
        using namespace ric::params;

        for (int i = 0; i < Tides::kCount; ++i) {
            chips_[static_cast<size_t>(i)] = std::make_unique<TideChip>(i);
            chips_[static_cast<size_t>(i)]->onSelect = [this](int idx) { select(idx); };
            addAndMakeVisible(*chips_[static_cast<size_t>(i)]);
        }

        // LFO pages
        const char* rateIds[3]  = { ID::lfo1Rate, ID::lfo2Rate, ID::lfo3Rate };
        const char* shapeIds[3] = { ID::lfo1Shape, ID::lfo2Shape, ID::lfo3Shape };
        const char* syncIds[3]  = { ID::lfo1Sync, ID::lfo2Sync, ID::lfo3Sync };
        const char* divIds[3]   = { ID::lfo1Division, ID::lfo2Division, ID::lfo3Division };
        for (size_t i = 0; i < 3; ++i) {
            auto& page = lfoPages_[i];
            page.rate = std::make_unique<PoeticKnob>(apvts, rateIds[i], "Rate", Palette::kGold, KnobTier::Medium, std::nullopt, nullptr, &midiLearn);
            page.rate->setTooltipText("How quickly this tide rises and falls");
            page.shape = std::make_unique<ShapePicker>(*apvts.getParameter(shapeIds[i]), Palette::kGold);
            addChildComponent(*page.rate);
            addChildComponent(*page.shape);

            page.sync = std::make_unique<juce::TextButton>("Sync");
            page.sync->setClickingTogglesState(true);
            page.sync->setColour(juce::TextButton::buttonOnColourId, Palette::kGold);
            page.sync->setTooltip("Follow the host tempo, locked to the bar while it plays");
            page.syncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, syncIds[i], *page.sync);
            page.division = std::make_unique<juce::ComboBox>();
            page.division->addItemList(kLfoDivisionNames, 1);
            page.division->setColour(juce::ComboBox::focusedOutlineColourId, Palette::kGold);
            page.division->setTooltip("One full rise and fall per ...");
            page.divisionAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, divIds[i], *page.division);
            addChildComponent(*page.sync);
            addChildComponent(*page.division);
        }
        addChildComponent(trace_);

        // ENV pages
        const char* aIds[3] = { ID::env1Attack, ID::env2Attack, ID::env3Attack };
        const char* dIds[3] = { ID::env1Decay, ID::env2Decay, ID::env3Decay };
        const char* sIds[3] = { ID::env1Sustain, ID::env2Sustain, ID::env3Sustain };
        const char* rIds[3] = { ID::env1Release, ID::env2Release, ID::env3Release };
        for (size_t i = 0; i < 3; ++i) {
            auto& page = envPages_[i];
            page.view = std::make_unique<EnvelopeView>(apvts, aIds[i], dIds[i], sIds[i], rIds[i]);
            page.knobs[0] = std::make_unique<PoeticKnob>(apvts, aIds[i], "Attack", Palette::kRose, KnobTier::Trim, std::nullopt, nullptr, &midiLearn);
            page.knobs[1] = std::make_unique<PoeticKnob>(apvts, dIds[i], "Decay", Palette::kRose, KnobTier::Trim, std::nullopt, nullptr, &midiLearn);
            page.knobs[2] = std::make_unique<PoeticKnob>(apvts, sIds[i], "Sustain", Palette::kRose, KnobTier::Trim, std::nullopt, nullptr, &midiLearn);
            page.knobs[3] = std::make_unique<PoeticKnob>(apvts, rIds[i], "Release", Palette::kRose, KnobTier::Trim, std::nullopt, nullptr, &midiLearn);
            addChildComponent(*page.view);
            for (auto& k : page.knobs) addChildComponent(*k);
        }

        // Loom page
        auto setupToggle = [&](juce::TextButton& b, const char* id, juce::Colour c, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att) {
            b.setClickingTogglesState(true);
            b.setColour(juce::TextButton::buttonOnColourId, c);
            addChildComponent(b);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, id, b);
        };
        setupToggle(loomOn_, ID::marblesEnable, Palette::kIris, loomOnAttach_);
        setupToggle(loomSync_, ID::marblesTempoSync, Palette::kGold, loomSyncAttach_);
        loomOn_.setTooltip("Let the loom play: it weaves notes and strums on its own");
        loomSync_.setTooltip("Follow the host tempo instead of the free rate");
        loomDivision_.addItemList(kMarblesDivisionNames, 1);
        loomDivision_.setColour(juce::ComboBox::focusedOutlineColourId, Palette::kIris);
        addChildComponent(loomDivision_);
        loomDivisionAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, ID::marblesDivision, loomDivision_);
        loomDivision_.setTooltip("The loom's step length when following the host");
        loomTarget_.addItemList(kLoomPitchTargetNames, 1);
        loomTarget_.setColour(juce::ComboBox::focusedOutlineColourId, Palette::kIris);
        loomTarget_.setTooltip("Where the loom's melody goes: the voice, the body, both - or nowhere, keeping only its rhythm");
        addChildComponent(loomTarget_);
        loomTargetAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, ID::marblesPitchTarget, loomTarget_);
        addChildComponent(loomView_);

        struct LoomKnobDef { const char* id; const char* label; const char* tip; };
        const LoomKnobDef loomDefs[6] = {
            { ID::marblesRate,   "Rate",    "Free-running pace of the loom" },
            { ID::marblesDejaVu, "Deja Vu", "Left: always new. Centre: the loop repeats. Right: the loop is shuffled" },
            { ID::marblesSpread, "Spread",  "How far the woven notes wander from the centre" },
            { ID::marblesSteps,  "Steps",   "Smooth glides below the middle, scale steps above it" },
            { ID::marblesLength, "Length",  "How many steps the loop remembers" },
            { ID::marblesJitter, "Drift",   "How loosely the loom keeps time" },
        };
        for (size_t i = 0; i < loomKnobs_.size(); ++i) {
            loomKnobs_[i] = std::make_unique<PoeticKnob>(apvts, loomDefs[i].id, loomDefs[i].label, Palette::kIris, KnobTier::Trim, std::nullopt, nullptr, &midiLearn);
            loomKnobs_[i]->setTooltipText(loomDefs[i].tip);
            addChildComponent(*loomKnobs_[i]);
        }

        // Routing
        addAndMakeVisible(routing_);
        touchBtn_.setClickingTogglesState(true);
        touchBtn_.setColour(juce::TextButton::buttonOnColourId, Palette::kRose);
        touchBtn_.setTooltip("While lit, dragging any dial up or down sets how far this tide moves it");
        touchBtn_.onClick = [this]() { notifyAssignMode(); };
        addAndMakeVisible(touchBtn_);
        stillBtn_.setTooltip("Disconnect this tide from every dial");
        stillBtn_.onClick = [this]() {
            if (routing_.isDestMode()) matrix_.clearConnectionsForDest(routing_.getDest());
            else matrix_.clearConnectionsForSource(static_cast<ric::mod::Source>(selected_));
            if (onRoutingChanged) onRoutingChanged();
        };
        addAndMakeVisible(stillBtn_);

        select(0);
    }

    int getSelected() const noexcept { return selected_; }

    std::vector<PoeticKnob*> getKnobs() {
        std::vector<PoeticKnob*> out;
        for (auto& p : lfoPages_) out.push_back(p.rate.get());
        for (auto& p : envPages_) for (auto& k : p.knobs) out.push_back(k.get());
        for (auto& k : loomKnobs_) out.push_back(k.get());
        return out;
    }

    void select(int idx) {
        selected_ = std::clamp(idx, 0, Tides::kCount - 1);
        for (int i = 0; i < Tides::kCount; ++i) chips_[static_cast<size_t>(i)]->setSelected(i == selected_);

        for (size_t i = 0; i < 3; ++i) {
            const bool vis = static_cast<int>(i) == selected_;
            lfoPages_[i].rate->setVisible(vis);
            lfoPages_[i].shape->setVisible(vis);
            lfoPages_[i].sync->setVisible(vis);
            lfoPages_[i].division->setVisible(vis);
        }
        trace_.setVisible(Tides::isLfo(selected_));
        trace_.setColour(Tides::colour(selected_));

        for (size_t i = 0; i < 3; ++i) {
            const bool vis = static_cast<int>(i) + 3 == selected_;
            envPages_[i].view->setVisible(vis);
            for (auto& k : envPages_[i].knobs) k->setVisible(vis);
        }

        const bool loom = Tides::isLoom(selected_);
        loomOn_.setVisible(loom);
        loomSync_.setVisible(loom);
        loomDivision_.setVisible(loom);
        loomTarget_.setVisible(loom);
        loomView_.setVisible(loom);
        for (auto& k : loomKnobs_) k->setVisible(loom);

        routing_.setSource(selected_);
        stillBtn_.setTooltip("Disconnect this tide from every dial");
        touchBtn_.setColour(juce::TextButton::buttonOnColourId, Tides::colour(selected_));
        notifyAssignMode();
        resized();
        repaint();
    }

    void advance(const ric::core::VisualSnapshot& snap) {
        for (int i = 0; i < Tides::kCount; ++i) {
            const float v = snap.modSourceValues[static_cast<size_t>(i)];
            history_[static_cast<size_t>(i)][static_cast<size_t>(historyWrite_)] = v;
            chips_[static_cast<size_t>(i)]->push(v);
        }
        historyWrite_ = (historyWrite_ + 1) % Tides::kHistory;

        if (Tides::isLfo(selected_))
            trace_.setHistory(history_[static_cast<size_t>(selected_)], historyWrite_);
        if (Tides::isEnv(selected_))
            envPages_[static_cast<size_t>(selected_ - 3)].view->setLiveLevel(snap.modSourceValues[static_cast<size_t>(selected_)]);
        if (Tides::isLoom(selected_))
            loomView_.update(snap);

        routing_.refreshIfChanged();
        updateChipSubtitles();
    }

    void refreshRouting() {
        if (routing_.isDestMode()) routing_.setDest(routing_.getDest());
        else routing_.setSource(selected_);
    }

    // A dial was touched: list the tides that move it
    void focusDestination(ric::mod::Dest d) {
        if (routing_.isDestMode() && routing_.getDest() == d) return;
        routing_.setDest(d);
        stillBtn_.setTooltip("Disconnect every tide from " + ric::mod::getDestName(d));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        Draw::card(g, getLocalBounds().toFloat());
        auto head = getLocalBounds().reduced(18, 0).removeFromTop(34);
        g.setFont(Fonts::serif(17.0f, true));
        g.setColour(Palette::kMoon);
        g.drawText("the tides", head.removeFromLeft(90), juce::Justification::centredLeft);
        if (touchBtn_.getToggleState()) {
            // Touch mode must be impossible to forget
            g.setFont(Fonts::caps(9.5f, true));
            g.setColour(Palette::kRose);
            g.drawText("TOUCH IS ON  -  DRAG ANY GLOWING DIAL UP OR DOWN TO SET HOW FAR " + Tides::title(selected_)
                           + " MOVES IT  -  PRESS TOUCH AGAIN TO FINISH",
                       head.withTrimmedRight(routingWidth() + 30), juce::Justification::centredLeft);
            g.setColour(Palette::kRose.withAlpha(0.7f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.8f), 14.0f, 1.6f);
        } else {
            g.setFont(Fonts::caps(9.0f));
            g.setColour(Palette::kFaint);
            g.drawText("MODULATION  -  DRAG A TIDE ONTO ANY DIAL", head, juce::Justification::centredLeft);
        }

        // Selected tide's poem above its editor
        g.setFont(Fonts::serif(12.5f, true));
        g.setColour(Tides::colour(selected_).withAlpha(0.85f));
        g.drawText(Tides::title(selected_) + "  -  " + Tides::poem(selected_), editorArea().withHeight(16).translated(0, -18),
                   juce::Justification::centredLeft);

        // Routing column header
        const auto rc = routingArea();
        g.setColour(Palette::kEdgeSoft);
        g.drawVerticalLine(rc.getX() - 12, static_cast<float>(rc.getY() - 30), static_cast<float>(getHeight() - 14));
        g.setFont(Fonts::caps(9.0f, true));
        g.setColour(Palette::kMist);
        g.drawText(routing_.isDestMode() ? "WHAT MOVES " + ric::mod::getDestName(routing_.getDest()).toUpperCase()
                                         : juce::String("WHERE IT FLOWS"),
                   rc.withHeight(14).translated(0, -22).withTrimmedRight(140), juce::Justification::centredLeft);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16, 0);
        area.removeFromTop(34);
        auto chipRow = area.removeFromTop(44).withTrimmedRight(routingWidth() + 24);
        const int chipW = (chipRow.getWidth() - 6 * 8) / Tides::kCount;
        for (int i = 0; i < Tides::kCount; ++i)
            chips_[static_cast<size_t>(i)]->setBounds(chipRow.getX() + i * (chipW + 8), chipRow.getY(), chipW, chipRow.getHeight());

        const auto rc = routingArea();
        auto buttons = rc.withHeight(24).translated(0, -30).withTrimmedLeft(rc.getWidth() - 132);
        touchBtn_.setBounds(buttons.removeFromLeft(66));
        buttons.removeFromLeft(6);
        stillBtn_.setBounds(buttons);
        routing_.setBounds(rc);

        auto ed = editorArea();
        if (Tides::isLfo(selected_)) {
            auto& page = lfoPages_[static_cast<size_t>(selected_)];
            trace_.setBounds(ed.removeFromLeft(320));
            ed.removeFromLeft(18);
            page.shape->setBounds(ed.removeFromLeft(190).withSizeKeepingCentre(190, 36));
            ed.removeFromLeft(14);
            auto syncCol = ed.removeFromLeft(118).withSizeKeepingCentre(118, 56);
            page.sync->setBounds(syncCol.removeFromTop(25).withWidth(70));
            syncCol.removeFromTop(5);
            page.division->setBounds(syncCol);
            ed.removeFromLeft(6);
            page.rate->setBounds(ed.removeFromLeft(90));
        } else if (Tides::isEnv(selected_)) {
            auto& page = envPages_[static_cast<size_t>(selected_ - 3)];
            page.view->setBounds(ed.removeFromLeft(320));
            ed.removeFromLeft(14);
            for (auto& k : page.knobs) k->setBounds(ed.removeFromLeft(72));
        } else {
            loomView_.setBounds(ed.removeFromLeft(270));
            ed.removeFromLeft(12);
            auto toggles = ed.removeFromLeft(92).withSizeKeepingCentre(92, 54);
            loomOn_.setBounds(toggles.removeFromTop(25));
            toggles.removeFromTop(4);
            loomSync_.setBounds(toggles.removeFromTop(25));
            ed.removeFromLeft(8);
            auto comboCol = ed.removeFromLeft(118).withSizeKeepingCentre(118, 56);
            loomDivision_.setBounds(comboCol.removeFromTop(26));
            comboCol.removeFromTop(4);
            loomTarget_.setBounds(comboCol);
            ed.removeFromLeft(4);
            const int kw = std::min(66, ed.getWidth() / static_cast<int>(loomKnobs_.size()));
            for (auto& k : loomKnobs_) k->setBounds(ed.removeFromLeft(kw));
        }
    }

private:
    struct LfoPage {
        std::unique_ptr<PoeticKnob> rate;
        std::unique_ptr<ShapePicker> shape;
        std::unique_ptr<juce::TextButton> sync;
        std::unique_ptr<juce::ComboBox> division;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttach;
    };
    struct EnvPage { std::unique_ptr<EnvelopeView> view; std::array<std::unique_ptr<PoeticKnob>, 4> knobs; };

    static int routingWidth() { return 300; }

    juce::Rectangle<int> routingArea() const {
        return { getWidth() - 16 - routingWidth(), 70, routingWidth(), getHeight() - 70 - 12 };
    }

    juce::Rectangle<int> editorArea() const {
        return { 16, 104, getWidth() - 32 - routingWidth() - 24, getHeight() - 104 - 8 };
    }

    void notifyAssignMode() {
        repaint();
        if (onAssignModeChanged)
            onAssignModeChanged(touchBtn_.getToggleState()
                                    ? std::optional<ric::mod::Source>(static_cast<ric::mod::Source>(selected_))
                                    : std::nullopt);
    }

    void updateChipSubtitles() {
        using namespace ric::params;
        const char* rateIds[3]  = { ID::lfo1Rate, ID::lfo2Rate, ID::lfo3Rate };
        const char* shapeIds[3] = { ID::lfo1Shape, ID::lfo2Shape, ID::lfo3Shape };
        const char* syncIds[3]  = { ID::lfo1Sync, ID::lfo2Sync, ID::lfo3Sync };
        const char* divIds[3]   = { ID::lfo1Division, ID::lfo2Division, ID::lfo3Division };
        static const char* kShapeWords[4] = { "sine", "tri", "saw", "s&h" };
        for (int i = 0; i < 3; ++i) {
            const float rate = apvts_.getRawParameterValue(rateIds[i])->load();
            const int shape = std::clamp(static_cast<int>(apvts_.getRawParameterValue(shapeIds[i])->load()), 0, 3);
            const bool synced = apvts_.getRawParameterValue(syncIds[i])->load() > 0.5f;
            const int div = std::clamp(static_cast<int>(apvts_.getRawParameterValue(divIds[i])->load()), 0, kLfoDivisionNames.size() - 1);
            chips_[static_cast<size_t>(i)]->setSubtitle(juce::String(kShapeWords[shape]) + " "
                + (synced ? kLfoDivisionNames[div] : juce::String(rate, rate < 1.0f ? 2 : 1) + "Hz"));
            lfoPages_[static_cast<size_t>(i)].rate->setDimmed(synced);
        }
        chips_[3]->setSubtitle("amp & gate");
        chips_[4]->setSubtitle("per note");
        chips_[5]->setSubtitle("per note");
        const bool loomOn = apvts_.getRawParameterValue(ID::marblesEnable)->load() > 0.5f;
        chips_[6]->setSubtitle(loomOn ? "weaving" : "resting");

        std::array<int, Tides::kCount> counts{};
        for (const auto& s : matrix_.getSlots())
            if (s.enabled) ++counts[static_cast<size_t>(s.source)];
        for (int i = 0; i < Tides::kCount; ++i) chips_[static_cast<size_t>(i)]->setRouteCount(counts[static_cast<size_t>(i)]);
    }

    juce::AudioProcessorValueTreeState& apvts_;
    ric::mod::ModMatrix& matrix_;
    int selected_ = 0;

    std::array<std::unique_ptr<TideChip>, Tides::kCount> chips_;
    std::array<std::array<float, Tides::kHistory>, Tides::kCount> history_{};
    int historyWrite_ = 0;

    std::array<LfoPage, 3> lfoPages_;
    TideTrace trace_;
    std::array<EnvPage, 3> envPages_;

    juce::TextButton loomOn_{ "Weave" };
    juce::TextButton loomSync_{ "Host Sync" };
    juce::ComboBox loomDivision_;
    juce::ComboBox loomTarget_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loomTargetAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loomOnAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loomSyncAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loomDivisionAttach_;
    LoomView loomView_;
    std::array<std::unique_ptr<PoeticKnob>, 6> loomKnobs_;

    RoutingList routing_;
    juce::TextButton touchBtn_{ "Touch" };
    juce::TextButton stillBtn_{ "Still" };
};

} // namespace ric::ui
