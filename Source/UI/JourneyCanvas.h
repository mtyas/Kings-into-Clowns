#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PoeticLookAndFeel.h"
#include "../Core/LockFreeVisualFifo.h"
#include "../Core/ParameterDefinitions.h"
#include <array>
#include <cmath>
#include <functional>

namespace ric::ui {

// The hero visual: one sky across the three stages.
//   I.   the breath - an ember orb drawn from the exciter waveform
//   II.  the body   - resonating rings, one per sounding voice (click to strum)
//   III. the sky    - the cloud's memory as a silhouette above its reflection, grains drifting in it
//                     (drag to move the playhead through time and the grain size)
// Motes of light carry the sound from left to right.
class JourneyCanvas : public juce::Component {
public:
    std::function<void(int midiNote, float velocity)> onStrum;

    explicit JourneyCanvas(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) {
        using namespace ric::params::ID;
        pExciterBypass_ = apvts_.getRawParameterValue(exciterBypass);
        pRingsBypass_   = apvts_.getRawParameterValue(ringsBypass);
        pCloudsBypass_  = apvts_.getRawParameterValue(cloudsBypass);
        pOscEngine_     = apvts_.getRawParameterValue(oscEngine);
        pRingsModel_    = apvts_.getRawParameterValue(ringsModel);
        pCloudsMode_    = apvts_.getRawParameterValue(cloudsMode);
        pInputSource_   = apvts_.getRawParameterValue(inputSource);

        juce::Random rng(0x51A7);
        for (auto& s : stars_) {
            s.x = rng.nextFloat();
            s.y = std::pow(rng.nextFloat(), 1.6f) * 0.8f;
            s.size = 0.6f + rng.nextFloat() * 1.3f;
            s.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            s.rate = 0.4f + rng.nextFloat() * 1.6f;
        }
        setOpaque(true);
    }

    // x positions (local coords) where stage II and stage III begin
    void setZoneSplits(float bodyStartX, float skyStartX) {
        bodyStart_ = bodyStartX;
        skyStart_ = skyStartX;
        background_ = {};
        repaint();
    }

    void advance(const ric::core::VisualSnapshot& snap, float dt) {
        snap_ = snap;
        time_ += dt;
        smoothExciter_ += (std::min(1.0f, snap.exciterEnergy * 3.0f) - smoothExciter_) * 0.25f;
        smoothBody_ += (std::min(1.0f, snap.resonatorEnergy * 2.2f) - smoothBody_) * 0.2f;
        for (size_t v = 0; v < voicePhase_.size(); ++v)
            voicePhase_[v] += dt * (0.6f + snap.voices[v].energy * 6.0f) * (v % 2 == 0 ? 1.0f : -1.0f);

        updateMotes(dt);
        updateRipples(dt);

        if (snap.loomTrigT1 || snap.loomTrigT3) spawnShootingStar();
        for (auto& s : shooting_) if (s.life > 0.0f) s.life -= dt * 1.4f;

        repaint();
    }

    //==========================================================================
    void resized() override { background_ = {}; }

    void paint(juce::Graphics& g) override {
        if (background_.isNull() || background_.getWidth() != getWidth() || background_.getHeight() != getHeight())
            renderBackground();
        g.drawImageAt(background_, 0, 0);

        drawStars(g);
        drawShootingStars(g);
        drawSky(g);
        drawMotes(g);
        drawBody(g);
        drawBreath(g);
        drawRipples(g);
        drawZoneTitles(g);
    }

    //==========================================================================
    void mouseDown(const juce::MouseEvent& e) override {
        const float x = static_cast<float>(e.x);
        if (x < bodyStart_) {
            // Breathe a note into the voice: height chooses the octave
            const float ny = 1.0f - std::clamp(static_cast<float>(e.y) / static_cast<float>(getHeight()), 0.0f, 1.0f);
            const int note = 48 + static_cast<int>(ny * 2.99f) * 12;
            if (onStrum) onStrum(note, 0.7f);
            addRipple(e.position, Palette::kEmber);
        } else if (x < skyStart_) {
            // Strum the body: pentatonic notes around the rings, velocity from distance to the centre
            static constexpr int kPentatonic[10] = { 45, 48, 50, 52, 55, 57, 60, 62, 64, 67 };
            const auto c = bodyCentre();
            const float ang = std::atan2(e.position.y - c.y, e.position.x - c.x) + juce::MathConstants<float>::pi;
            const int idx = std::clamp(static_cast<int>(ang / juce::MathConstants<float>::twoPi * 10.0f), 0, 9);
            const float dist = e.position.getDistanceFrom(c) / (static_cast<float>(getHeight()) * 0.45f);
            if (onStrum) onStrum(kPentatonic[idx], std::clamp(1.05f - dist * 0.6f, 0.35f, 1.0f));
            addRipple(e.position, Palette::kJade);
        } else {
            draggingSky_ = true;
            beginGesture(ric::params::ID::cloudsPosition);
            beginGesture(ric::params::ID::cloudsSize);
            dragSky(e.position);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (draggingSky_) dragSky(e.position);
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (draggingSky_) {
            draggingSky_ = false;
            endGesture(ric::params::ID::cloudsPosition);
            endGesture(ric::params::ID::cloudsSize);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const float x = static_cast<float>(e.x);
        setMouseCursor(x >= skyStart_ ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::PointingHandCursor);
    }

private:
    struct Star { float x, y, size, phase, rate; };
    struct Mote { float t = 0.0f, speed = 0.0f, wobble = 0.0f, size = 1.0f; bool alive = false; };
    struct Ripple { juce::Point<float> p; juce::Colour c; float life = 0.0f; };
    struct ShootingStar { juce::Point<float> from; float angle = 0.0f; float life = 0.0f; };

    static constexpr size_t kMaxMotes = 90;

    bool param(std::atomic<float>* p) const { return p != nullptr && p->load() > 0.5f; }
    int paramInt(std::atomic<float>* p) const { return p != nullptr ? static_cast<int>(p->load()) : 0; }

    float midY() const { return static_cast<float>(getHeight()) * 0.52f; }
    juce::Point<float> breathCentre() const { return { bodyStart_ * 0.5f, midY() }; }
    juce::Point<float> bodyCentre() const { return { (bodyStart_ + skyStart_) * 0.5f, midY() }; }
    float horizonY() const { return static_cast<float>(getHeight()) * 0.68f; }
    float skyLeft() const { return skyStart_ + 26.0f; }
    float skyRight() const { return static_cast<float>(getWidth()) - 26.0f; }
    float skyX(float delay) const { return skyLeft() + std::clamp(delay, 0.0f, 1.0f) * (skyRight() - skyLeft()); }

    //==========================================================================
    void renderBackground() {
        const int w = std::max(1, getWidth());
        const int h = std::max(1, getHeight());
        background_ = juce::Image(juce::Image::RGB, w, h, true);
        juce::Graphics g(background_);
        const auto r = getLocalBounds().toFloat();

        juce::ColourGradient sky(juce::Colour(0xFF070913), 0.0f, 0.0f, juce::Colour(0xFF1B1934), 0.0f, r.getBottom(), false);
        sky.addColour(0.7, juce::Colour(0xFF151530));
        g.setGradientFill(sky);
        g.fillRect(r);

        // Horizon light: warm under the breath, green under the body, cool under the sky
        const float hy = r.getBottom() + r.getHeight() * 0.15f;
        Draw::glow(g, { bodyStart_ * 0.5f, hy }, r.getHeight() * 0.95f, Palette::kCoral, 0.22f);
        Draw::glow(g, { (bodyStart_ + skyStart_) * 0.5f, hy }, r.getHeight() * 0.95f, Palette::kJade, 0.16f);
        Draw::glow(g, { (skyStart_ + r.getRight()) * 0.5f, hy }, r.getHeight() * 1.1f, Palette::kPearl, 0.18f);

        // Zone seams, barely there
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        for (float sx : { bodyStart_, skyStart_ })
            Draw::dashedLine(g, { sx, 18.0f, sx, r.getBottom() - 18.0f }, 2.0f, 6.0f, 1.0f);

        // Still water line in the sky
        g.setColour(Palette::kPearl.withAlpha(0.10f));
        g.drawHorizontalLine(static_cast<int>(horizonY()), skyLeft(), skyRight());

        // Concentric guides in the body
        const auto c = bodyCentre();
        const float maxR = std::min(skyStart_ - bodyStart_, r.getHeight()) * 0.44f;
        for (int i = 1; i <= 4; ++i) {
            const float rr = maxR * static_cast<float>(i) / 4.0f;
            g.setColour(Palette::kJade.withAlpha(0.035f + 0.01f * static_cast<float>(i)));
            g.drawEllipse(c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 1.0f);
        }

        // Vignette
        juce::ColourGradient vig(juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                 juce::Colours::black.withAlpha(0.35f), 0.0f, 0.0f, true);
        g.setGradientFill(vig);
        g.fillRect(r);
    }

    void drawStars(juce::Graphics& g) {
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());
        for (const auto& s : stars_) {
            const float tw = 0.5f + 0.5f * std::sin(time_ * s.rate + s.phase);
            g.setColour(Palette::kMoon.withAlpha(0.10f + 0.35f * tw * s.size * 0.6f));
            const float sz = s.size * (0.8f + 0.4f * tw);
            g.fillEllipse(s.x * w - sz * 0.5f, s.y * h - sz * 0.5f, sz, sz);
        }
    }

    void spawnShootingStar() {
        for (auto& s : shooting_) {
            if (s.life <= 0.0f) {
                s.from = { juce::Random::getSystemRandom().nextFloat() * bodyStart_ + 20.0f, 14.0f + juce::Random::getSystemRandom().nextFloat() * 30.0f };
                s.angle = 0.35f + juce::Random::getSystemRandom().nextFloat() * 0.3f;
                s.life = 1.0f;
                return;
            }
        }
    }

    void drawShootingStars(juce::Graphics& g) {
        for (const auto& s : shooting_) {
            if (s.life <= 0.0f) continue;
            const float travel = (1.0f - s.life) * 120.0f;
            const juce::Point<float> head(s.from.x + std::cos(s.angle) * travel, s.from.y + std::sin(s.angle) * travel);
            const juce::Point<float> tail(head.x - std::cos(s.angle) * 34.0f, head.y - std::sin(s.angle) * 34.0f);
            juce::ColourGradient grad(Palette::kRose.withAlpha(0.0f), tail.x, tail.y, Palette::kRose.withAlpha(0.8f * s.life), head.x, head.y, false);
            g.setGradientFill(grad);
            g.drawLine({ tail, head }, 1.6f);
            Draw::glow(g, head, 6.0f, Palette::kRose, s.life);
        }
    }

    //==========================================================================
    void drawBreath(juce::Graphics& g) {
        const auto c = breathCentre();
        const bool bypassed = param(pExciterBypass_) || paramInt(pInputSource_) == 1;
        const float baseR = std::min(bodyStart_, static_cast<float>(getHeight())) * 0.17f;
        const auto col = bypassed ? Palette::kAsh : Palette::kEmber;

        Draw::glow(g, c, baseR * (2.4f + smoothExciter_ * 1.2f), Palette::kCoral, bypassed ? 0.05f : 0.18f + smoothExciter_ * 0.45f);

        // Polar waveform: the voice traced around itself
        float peak = 0.02f;
        for (float v : snap_.exciterWave) peak = std::max(peak, std::abs(v));
        const float gain = 0.55f / std::max(0.25f, peak);
        juce::Path orb;
        const size_t n = snap_.exciterWave.size();
        for (size_t i = 0; i <= n; ++i) {
            const size_t idx = i % n;
            const float a = static_cast<float>(i) / static_cast<float>(n) * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
            const float rr = baseR * (1.0f + std::clamp(snap_.exciterWave[idx] * gain, -0.6f, 0.9f) * (0.25f + smoothExciter_ * 0.75f));
            const juce::Point<float> p(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
            if (i == 0) orb.startNewSubPath(p); else orb.lineTo(p);
        }
        orb.closeSubPath();
        g.setColour(col.withAlpha(0.14f));
        g.fillPath(orb);
        g.setColour(col.withAlpha(0.85f));
        g.strokePath(orb, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));

        // Core
        Draw::glow(g, c, baseR * 0.55f, Palette::kGold, bypassed ? 0.1f : 0.5f + smoothExciter_ * 0.5f);
        g.setColour(Palette::kMoon.withAlpha(bypassed ? 0.3f : 0.9f));
        g.fillEllipse(c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);

        // Three satellites: harmonics, timbre and morph as they move under modulation
        static constexpr ric::mod::Dest kSat[3] = { ric::mod::Dest::OscHarmonics, ric::mod::Dest::OscTimbre, ric::mod::Dest::OscMorph };
        for (int i = 0; i < 3; ++i) {
            const float v = snap_.modulatedNormalized[static_cast<size_t>(kSat[i])];
            const float orbitR = baseR * (1.55f + 0.32f * static_cast<float>(i));
            const float a = v * juce::MathConstants<float>::twoPi * 0.9f + static_cast<float>(i) * 2.1f - juce::MathConstants<float>::halfPi;
            const juce::Point<float> p(c.x + std::cos(a) * orbitR, c.y + std::sin(a) * orbitR);
            g.setColour(col.withAlpha(0.10f));
            g.drawEllipse(c.x - orbitR, c.y - orbitR, orbitR * 2.0f, orbitR * 2.0f, 0.8f);
            Draw::glow(g, p, 6.0f, col, bypassed ? 0.15f : 0.7f);
            g.setColour(col);
            g.fillEllipse(p.x - 1.8f, p.y - 1.8f, 3.6f, 3.6f);
        }

        const juce::String caption = paramInt(pInputSource_) == 1
            ? juce::String("listening outward")
            : (bypassed ? juce::String("the breath is held")
                        : ric::params::kOscEngineNames[std::clamp(paramInt(pOscEngine_), 0, ric::params::kOscEngineNames.size() - 1)]);
        drawCaption(g, caption, { 0.0f, 0.0f, bodyStart_, static_cast<float>(getHeight()) });
    }

    //==========================================================================
    void drawBody(juce::Graphics& g) {
        const auto c = bodyCentre();
        const bool bypassed = param(pRingsBypass_);
        const float maxR = std::min(skyStart_ - bodyStart_, static_cast<float>(getHeight())) * 0.44f;
        const float minR = maxR * 0.16f;
        const float structure = snap_.modulatedNormalized[static_cast<size_t>(ric::mod::Dest::RingsStructure)];
        const float brightness = snap_.modulatedNormalized[static_cast<size_t>(ric::mod::Dest::RingsBrightness)];
        const float position = snap_.modulatedNormalized[static_cast<size_t>(ric::mod::Dest::RingsPosition)];
        const int lobes = 2 + static_cast<int>(std::round(structure * 9.0f));

        Draw::glow(g, c, maxR * 1.25f, Palette::kJade, bypassed ? 0.03f : 0.06f + smoothBody_ * 0.28f);

        const auto ringCol = bypassed ? Palette::kAsh : Palette::kJade;
        bool anyVoice = false;
        for (size_t v = 0; v < snap_.voices.size(); ++v) {
            const auto& vv = snap_.voices[v];
            if (!vv.active) continue;
            anyVoice = true;
            const float noteNorm = std::clamp((vv.note - 28.0f) / 72.0f, 0.0f, 1.0f);
            const float rr = minR + (1.0f - noteNorm) * (maxR - minR);
            const float energy = std::min(1.0f, vv.energy * 3.0f);
            const float amp = rr * 0.12f * energy;

            juce::Path ring;
            constexpr int kPts = 120;
            for (int i = 0; i <= kPts; ++i) {
                const float a = static_cast<float>(i) / kPts * juce::MathConstants<float>::twoPi;
                const float wob = std::sin(a * static_cast<float>(lobes) + voicePhase_[v] * 3.0f)
                                + 0.35f * std::sin(a * static_cast<float>(lobes * 2 + 1) - voicePhase_[v] * 5.0f) * structure;
                const float r2 = rr + amp * wob;
                const juce::Point<float> p(c.x + std::cos(a) * r2, c.y + std::sin(a) * r2);
                if (i == 0) ring.startNewSubPath(p); else ring.lineTo(p);
            }
            const auto col = ringCol.interpolatedWith(Palette::kPearl, brightness * 0.5f);
            g.setColour(col.withAlpha(0.08f + 0.25f * energy));
            g.strokePath(ring, juce::PathStrokeType(4.0f + energy * 5.0f));
            g.setColour(col.withAlpha(0.35f + 0.6f * energy * (vv.gate ? 1.0f : 0.75f)));
            g.strokePath(ring, juce::PathStrokeType(1.3f));

            // Where the body is struck
            const float sa = position * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
            const juce::Point<float> sp(c.x + std::cos(sa) * rr, c.y + std::sin(sa) * rr);
            Draw::glow(g, sp, 5.0f, Palette::kGold, 0.4f + energy * 0.6f);
        }

        if (!anyVoice) {
            // Resting: a single breathing ring
            const float rr = (minR + maxR) * 0.45f * (1.0f + 0.03f * std::sin(time_ * 0.9f));
            g.setColour(ringCol.withAlpha(0.22f));
            g.drawEllipse(c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 1.0f);
        }

        Draw::glow(g, c, minR * 0.8f, ringCol, 0.25f + smoothBody_ * 0.5f);

        const juce::String caption = bypassed
            ? juce::String("the body is silent")
            : ric::params::kRingsModelNames[std::clamp(paramInt(pRingsModel_), 0, ric::params::kRingsModelNames.size() - 1)];
        drawCaption(g, caption, { bodyStart_, 0.0f, skyStart_ - bodyStart_, static_cast<float>(getHeight()) });
    }

    //==========================================================================
    void drawSky(juce::Graphics& g) {
        const bool bypassed = param(pCloudsBypass_);
        const float hy = horizonY();
        const float h = static_cast<float>(getHeight());
        const auto cloudCol = bypassed ? Palette::kAsh : Palette::kPearl;
        const float reverb = snap_.modulatedNormalized[static_cast<size_t>(ric::mod::Dest::CloudsReverb)];
        const float mix = snap_.modulatedNormalized[static_cast<size_t>(ric::mod::Dest::CloudsDryWet)];

        // Reverb haze
        if (!bypassed)
            Draw::glow(g, { (skyLeft() + skyRight()) * 0.5f, hy - h * 0.2f }, (skyRight() - skyLeft()) * 0.6f, Palette::kIris, reverb * 0.16f);

        // Memory: the last ~3 seconds as a cloud bank, newest at the left, dissolving as it ages
        float memPeak = 0.05f;
        for (float m : snap_.cloudsMemory) memPeak = std::max(memPeak, m);
        const float memGain = 1.0f / std::max(0.3f, memPeak);
        const size_t bins = snap_.cloudsMemory.size();
        for (int layer = 2; layer >= 0; --layer) {
            const float layerScale = 1.0f - 0.22f * static_cast<float>(layer);
            const float drift = time_ * (0.25f + 0.12f * static_cast<float>(layer));
            juce::Path top;
            juce::Point<float> prev(skyLeft(), hy);
            top.startNewSubPath(prev);
            for (size_t k = 0; k < bins; ++k) {
                const float u = static_cast<float>(k) / static_cast<float>(bins - 1);
                const float taper = juce::jlimit(0.0f, 1.0f, u * 14.0f) * (1.0f - 0.55f * u)
                                  * juce::jlimit(0.0f, 1.0f, (1.0f - u) / 0.14f);
                const float billow = 0.86f + 0.14f * std::sin(static_cast<float>(k) * 0.55f + drift + static_cast<float>(layer) * 1.7f)
                                           * std::sin(static_cast<float>(k) * 0.21f - drift * 0.6f);
                const float m = std::pow(std::clamp(snap_.cloudsMemory[k] * memGain, 0.0f, 1.0f), 0.7f);
                const juce::Point<float> pt(skyX(u), hy - m * taper * billow * layerScale * h * 0.44f);
                top.quadraticTo(prev, (prev + pt) * 0.5f);
                prev = pt;
            }
            top.lineTo(skyRight(), hy);
            top.closeSubPath();

            const float a = layer == 0 ? 0.30f : 0.10f;
            juce::ColourGradient fill(cloudCol.withAlpha(a), 0.0f, hy - h * 0.42f, cloudCol.withAlpha(a * 0.12f), 0.0f, hy, false);
            g.setGradientFill(fill);
            g.fillPath(top);
            if (layer == 0) {
                g.setColour(cloudCol.withAlpha(0.5f));
                g.strokePath(top, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved));
                // its reflection in still water
                juce::Path reflection(top);
                reflection.applyTransform(juce::AffineTransform::verticalFlip(hy * 2.0f)
                                              .followedBy(juce::AffineTransform::scale(1.0f, 0.38f, 0.0f, hy)));
                juce::ColourGradient refl(Palette::kIris.withAlpha(0.16f), 0.0f, hy, Palette::kIris.withAlpha(0.0f), 0.0f, hy + h * 0.17f, false);
                g.setGradientFill(refl);
                g.fillPath(reflection);
            }
        }

        // Playhead: where in time the grains are listening
        const float px = skyX(snap_.cloudsPlayhead);
        g.setColour(Palette::kGold.withAlpha(draggingSky_ ? 0.85f : 0.45f));
        Draw::dashedLine(g, { px, 22.0f, px, hy + h * 0.14f }, 3.0f, 4.0f, 1.0f);
        Draw::glow(g, { px, hy }, 8.0f, Palette::kGold, 0.7f);

        // Grains
        for (size_t i = 0; i < std::min<size_t>(snap_.activeGrains, snap_.grains.size()); ++i) {
            const auto& gr = snap_.grains[i];
            const float x = skyX(gr.delay);
            const float pitchOct = std::log2(std::max(0.25f, gr.pitchRatio));
            const float y = std::clamp(hy - h * 0.24f - pitchOct * h * 0.12f + (gr.pan - 0.5f) * h * 0.34f, 16.0f, hy - 6.0f);
            const float rad = 1.5f + gr.amplitude * 6.0f;
            const auto col = cloudCol.interpolatedWith(Palette::kIris, gr.pan);
            Draw::glow(g, { x, y }, rad * 2.2f, col, 0.18f + gr.amplitude * 0.5f * (0.4f + mix));
            g.setColour(Palette::kMoon.withAlpha(0.35f + gr.amplitude * 0.6f));
            g.fillEllipse(x - 1.4f, y - 1.4f, 2.8f, 2.8f);
            // reflection
            const float ry = hy + (hy - y) * 0.35f;
            g.setColour(col.withAlpha(0.08f + gr.amplitude * 0.15f));
            g.fillEllipse(x - rad * 0.6f, ry - rad * 0.25f, rad * 1.2f, rad * 0.5f);
        }

        // Frozen: frost over the window of time
        if (snap_.cloudsFrozen && !bypassed) {
            const juce::Rectangle<float> frost(skyLeft() - 10.0f, 38.0f, skyRight() - skyLeft() + 20.0f, h - 70.0f);
            g.setColour(Palette::kPearl.withAlpha(0.06f));
            g.fillRoundedRectangle(frost, 12.0f);
            g.setColour(Palette::kPearl.withAlpha(0.28f));
            juce::Path outline;
            outline.addRoundedRectangle(frost, 12.0f);
            juce::Path dashed;
            const float dashes[2] = { 2.0f, 5.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.fillPath(dashed);
            drawSnowflake(g, { skyRight() - 22.0f, 34.0f }, 11.0f);
        }

        // The time axis, in words
        g.setFont(Fonts::serif(11.5f, true));
        g.setColour(Palette::kFaint);
        g.drawText("now", juce::Rectangle<float>(skyLeft(), h - 22.0f, 80.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText("three seconds ago", juce::Rectangle<float>(skyRight() - 140.0f, h - 22.0f, 140.0f, 14.0f), juce::Justification::centredRight);

        const juce::String caption = bypassed ? juce::String("the sky is clear")
                                   : snap_.cloudsFrozen ? juce::String("frozen in time")
                                   : ric::params::kCloudsModeNames[std::clamp(paramInt(pCloudsMode_), 0, ric::params::kCloudsModeNames.size() - 1)];
        drawCaption(g, caption, { skyStart_, 0.0f, static_cast<float>(getWidth()) - skyStart_, static_cast<float>(getHeight()) });
    }

    void drawSnowflake(juce::Graphics& g, juce::Point<float> c, float r) {
        g.setColour(Palette::kPearl.withAlpha(0.8f));
        for (int i = 0; i < 6; ++i) {
            const float a = static_cast<float>(i) * juce::MathConstants<float>::pi / 3.0f;
            const juce::Point<float> tip(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
            g.drawLine({ c, tip }, 1.1f);
            const juce::Point<float> mid(c.x + std::cos(a) * r * 0.55f, c.y + std::sin(a) * r * 0.55f);
            for (float side : { -0.6f, 0.6f })
                g.drawLine({ mid, { mid.x + std::cos(a + side) * r * 0.3f, mid.y + std::sin(a + side) * r * 0.3f } }, 1.0f);
        }
        Draw::glow(g, c, r * 1.8f, Palette::kPearl, 0.35f);
    }

    //==========================================================================
    // Motes: light carried from the breath, through the body, into the sky
    juce::Point<float> motePosition(const Mote& m) const {
        const auto a = breathCentre();
        const auto b = bodyCentre();
        const juce::Point<float> e(skyLeft() + 8.0f, horizonY() - static_cast<float>(getHeight()) * 0.18f);
        const float t = m.t;
        juce::Point<float> p;
        if (t < 0.5f) {
            const float u = t * 2.0f;
            p = a + (b - a) * u;
            p.y -= std::sin(u * juce::MathConstants<float>::pi) * static_cast<float>(getHeight()) * 0.18f;
        } else {
            const float u = (t - 0.5f) * 2.0f;
            p = b + (e - b) * u;
            p.y -= std::sin(u * juce::MathConstants<float>::pi) * static_cast<float>(getHeight()) * 0.12f;
        }
        p.y += std::sin(time_ * 2.0f + m.wobble) * 6.0f * std::sin(t * juce::MathConstants<float>::pi);
        return p;
    }

    void updateMotes(float dt) {
        const float energy = std::clamp(smoothExciter_ + smoothBody_, 0.0f, 1.5f);
        spawnAccumulator_ += dt * energy * 38.0f;
        for (auto& m : motes_) {
            if (!m.alive) {
                if (spawnAccumulator_ >= 1.0f) {
                    spawnAccumulator_ -= 1.0f;
                    auto& rng = juce::Random::getSystemRandom();
                    m = { 0.0f, 0.16f + rng.nextFloat() * 0.14f, rng.nextFloat() * 6.28f, 0.8f + rng.nextFloat() * 1.4f, true };
                }
                continue;
            }
            m.t += dt * m.speed * (0.6f + energy * 0.6f);
            if (m.t >= 1.0f) m.alive = false;
        }
        spawnAccumulator_ = std::min(spawnAccumulator_, 3.0f);
    }

    void drawMotes(juce::Graphics& g) {
        for (const auto& m : motes_) {
            if (!m.alive) continue;
            const auto p = motePosition(m);
            const juce::Colour col = m.t < 0.5f ? Palette::kEmber.interpolatedWith(Palette::kJade, m.t * 2.0f)
                                                : Palette::kJade.interpolatedWith(Palette::kPearl, (m.t - 0.5f) * 2.0f);
            const float fade = std::sin(m.t * juce::MathConstants<float>::pi);
            g.setColour(col.withAlpha(0.25f * fade));
            g.fillEllipse(p.x - m.size * 2.0f, p.y - m.size * 2.0f, m.size * 4.0f, m.size * 4.0f);
            g.setColour(col.withAlpha(0.85f * fade));
            g.fillEllipse(p.x - m.size * 0.6f, p.y - m.size * 0.6f, m.size * 1.2f, m.size * 1.2f);
        }
    }

    //==========================================================================
    void addRipple(juce::Point<float> p, juce::Colour c) {
        for (auto& r : ripples_) {
            if (r.life <= 0.0f) { r = { p, c, 1.0f }; return; }
        }
    }

    void updateRipples(float dt) {
        for (auto& r : ripples_) if (r.life > 0.0f) r.life -= dt * 1.6f;
    }

    void drawRipples(juce::Graphics& g) {
        for (const auto& r : ripples_) {
            if (r.life <= 0.0f) continue;
            const float rad = (1.0f - r.life) * 46.0f + 4.0f;
            g.setColour(r.c.withAlpha(r.life * 0.7f));
            g.drawEllipse(r.p.x - rad, r.p.y - rad, rad * 2.0f, rad * 2.0f, 1.2f);
            const float rad2 = rad * 0.6f;
            g.setColour(r.c.withAlpha(r.life * 0.35f));
            g.drawEllipse(r.p.x - rad2, r.p.y - rad2, rad2 * 2.0f, rad2 * 2.0f, 1.0f);
        }
    }

    //==========================================================================
    void drawZoneTitles(juce::Graphics& g) {
        struct Title { const char* numeral; const char* name; float x; juce::Colour c; };
        const Title titles[3] = {
            { "I",   "the breath", 18.0f,              Palette::kEmber },
            { "II",  "the body",   bodyStart_ + 18.0f, Palette::kJade },
            { "III", "the sky",    skyStart_ + 18.0f,  Palette::kPearl },
        };
        for (const auto& t : titles) {
            g.setFont(Fonts::serif(12.0f));
            g.setColour(t.c.withAlpha(0.8f));
            g.drawText(t.numeral, juce::Rectangle<float>(t.x, 12.0f, 30.0f, 16.0f), juce::Justification::centredLeft);
            g.setFont(Fonts::serif(15.0f, true));
            g.setColour(Palette::kMoon.withAlpha(0.85f));
            g.drawText(t.name, juce::Rectangle<float>(t.x + 24.0f, 11.0f, 160.0f, 18.0f), juce::Justification::centredLeft);
        }
    }

    void drawCaption(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> zone) {
        g.setFont(Fonts::caps(9.0f));
        g.setColour(Palette::kFaint);
        g.drawText(text.toUpperCase(), zone.removeFromBottom(26.0f).withTrimmedBottom(8.0f).reduced(16.0f, 0.0f),
                   juce::Justification::centred);
    }

    //==========================================================================
    void dragSky(juce::Point<float> p) {
        const float pos = std::clamp((p.x - skyLeft()) / (skyRight() - skyLeft()), 0.0f, 1.0f);
        const float size = std::clamp(1.0f - (p.y - 16.0f) / (horizonY() - 16.0f), 0.0f, 1.0f);
        setParam(ric::params::ID::cloudsPosition, pos);
        setParam(ric::params::ID::cloudsSize, size);
    }

    void setParam(const char* id, float norm) {
        if (auto* prm = apvts_.getParameter(id)) prm->setValueNotifyingHost(norm);
    }
    void beginGesture(const char* id) { if (auto* prm = apvts_.getParameter(id)) prm->beginChangeGesture(); }
    void endGesture(const char* id) { if (auto* prm = apvts_.getParameter(id)) prm->endChangeGesture(); }

    //==========================================================================
    juce::AudioProcessorValueTreeState& apvts_;
    std::atomic<float>* pExciterBypass_ = nullptr;
    std::atomic<float>* pRingsBypass_ = nullptr;
    std::atomic<float>* pCloudsBypass_ = nullptr;
    std::atomic<float>* pOscEngine_ = nullptr;
    std::atomic<float>* pRingsModel_ = nullptr;
    std::atomic<float>* pCloudsMode_ = nullptr;
    std::atomic<float>* pInputSource_ = nullptr;

    ric::core::VisualSnapshot snap_{};
    juce::Image background_;
    float bodyStart_ = 380.0f;
    float skyStart_ = 810.0f;
    float time_ = 0.0f;
    float smoothExciter_ = 0.0f;
    float smoothBody_ = 0.0f;
    float spawnAccumulator_ = 0.0f;
    bool draggingSky_ = false;

    std::array<float, ric::core::VisualSnapshot::kMaxVoices> voicePhase_{};
    std::array<Star, 70> stars_{};
    std::array<Mote, kMaxMotes> motes_{};
    std::array<Ripple, 8> ripples_{};
    std::array<ShootingStar, 4> shooting_{};
};

} // namespace ric::ui
