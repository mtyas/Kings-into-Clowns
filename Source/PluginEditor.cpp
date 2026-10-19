#include "PluginEditor.h"
#include "Core/ParameterDefinitions.h"
#include "UI/PoeticKnob.h"
#include "UI/JourneyCanvas.h"
#include "UI/TidesPanel.h"
#include "UI/PresetBrowserModal.h"
#include "UI/AboutOverlay.h"
#include "UI/Widgets.h"
#include "UI/KnobRoles.h"
#include <array>
#include <optional>
#include <vector>

namespace ric::ui {

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
class MainView : public juce::Component,
                 public juce::DragAndDropContainer,
                 private juce::Timer {
public:
    explicit MainView(KingsIntoClownsProcessor& p)
        : processor_(p),
          canvas_(p.getAPVTS()),
          tides_(p.getAPVTS(), p.getSynthEngine().getModMatrix(), p.getMidiLearnManager()),
          presetBrowser_(p.getPresetManager(), [this]() { onPresetChanged(); }) {
        auto& apvts = processor_.getAPVTS();
        using namespace ric::params;
        using ric::mod::Dest;

        // ---- Header ---------------------------------------------------------
        prevPresetBtn_.onClick = [this]() { processor_.getPresetManager().stepPreset(-1); onPresetChanged(); };
        nextPresetBtn_.onClick = [this]() { processor_.getPresetManager().stepPreset(1); onPresetChanged(); };
        presetTitle_.onClick = [this]() { presetBrowser_.openBrowser(); };
        addAndMakeVisible(prevPresetBtn_);
        addAndMakeVisible(nextPresetBtn_);
        addAndMakeVisible(presetTitle_);

        const juce::StringArray kVoiceWords { "One voice", "Four voices", "Eight voices", "Sixteen voices" };
        setupCombo(polyphonyCombo_, kVoiceWords, ID::polyphonyMode, polyphonyAttach_, Palette::kGold);
        polyphonyCombo_.setTooltip("How many notes may sound at once (one voice glides legato)");

        undoBtn_.onClick = [this]() { processor_.getUndoManager().undo(); };
        redoBtn_.onClick = [this]() { processor_.getUndoManager().redo(); };
        addAndMakeVisible(undoBtn_);
        addAndMakeVisible(redoBtn_);

        learnBtn_.setClickingTogglesState(true);
        learnBtn_.setColour(juce::TextButton::buttonOnColourId, Palette::kRose);
        learnBtn_.setTooltip("MIDI learn: touch a dial, then move a controller");
        learnBtn_.onClick = [this]() {
            processor_.getMidiLearnManager().setLearnActive(learnBtn_.getToggleState());
            refreshAllBadges();
        };
        addAndMakeVisible(learnBtn_);

        aboutBtn_.onClick = [this]() { aboutOverlay_.setVisible(true); aboutOverlay_.toFront(true); aboutOverlay_.grabKeyboardFocus(); };
        addAndMakeVisible(aboutBtn_);

        masterKnob_ = makeKnob(ID::masterGain, "Out", Palette::kMoon, KnobTier::Trim, std::nullopt, "The final level, in decibels");

        // ---- Canvas ---------------------------------------------------------
        canvas_.onStrum = [this](int note, float vel) { processor_.triggerManualStrum(note, vel); };
        addAndMakeVisible(canvas_);

        // ---- I. The breath ---------------------------------------------------
        setupPower(exciterPower_, ID::exciterBypass, exciterPowerAttach_);
        setupCombo(engineCombo_, kOscEngineNames, ID::oscEngine, engineAttach_, Palette::kEmber);
        engineCombo_.setTooltip("The voice's character: twelve ways to breathe");
        const juce::StringArray kSourceWords { "Inner voice", "Outer audio", "Voice + audio" };
        setupCombo(sourceCombo_, kSourceWords, ID::inputSource, sourceAttach_, Palette::kEmber);
        sourceCombo_.setTooltip("Excite the body with the inner voice, with audio from the track, or both");

        stageKnobs_[0] = {
            makeKnob(ID::oscHarmonics, "Harmonics", Palette::kEmber, KnobTier::Hero, Dest::OscHarmonics, "Which overtones the voice remembers"),
            makeKnob(ID::oscTimbre,    "Timbre",    Palette::kEmber, KnobTier::Hero, Dest::OscTimbre,    "The brightness of the breath"),
            makeKnob(ID::oscMorph,     "Morph",     Palette::kEmber, KnobTier::Hero, Dest::OscMorph,     "How the voice changes its shape"),
            makeKnob(ID::oscTune,      "Tune",      Palette::kCoral, KnobTier::Medium, Dest::OscTune,     "Transpose the voice, in semitones"),
            makeKnob(ID::subBlend,     "Sub",       Palette::kCoral, KnobTier::Medium, Dest::SubBlend,    "A lower shadow beneath the voice"),
            makeKnob(ID::strikeLevel,  "Strike",    Palette::kCoral, KnobTier::Medium, Dest::StrikeLevel, "How hard the mallet strikes the body"),
            makeKnob(ID::directMix,    "Direct",    Palette::kCoral, KnobTier::Medium, Dest::DirectMix,   "Let some of the raw voice slip past the body"),
        };

        // ---- II. The body ------------------------------------------------------
        setupPower(ringsPower_, ID::ringsBypass, ringsPowerAttach_);
        strumBtn_.setColour(juce::TextButton::buttonOnColourId, Palette::kJade);
        strumBtn_.setTooltip("Pluck the body (or click the rings above)");
        strumBtn_.onClick = [this]() { processor_.triggerManualStrum(); };
        addAndMakeVisible(strumBtn_);
        setupCombo(modelCombo_, kRingsModelNames, ID::ringsModel, modelAttach_, Palette::kJade);
        modelCombo_.setTooltip("What the body is made of");
        const juce::StringArray kStringWords { "1 string", "2 strings", "4 strings" };
        setupCombo(stringsCombo_, kStringWords, ID::ringsPoly, stringsAttach_, Palette::kJade);
        stringsCombo_.setTooltip("Resonators inside each voice");

        stageKnobs_[1] = {
            makeKnob(ID::ringsStructure,  "Structure",  Palette::kJade, KnobTier::Hero, Dest::RingsStructure,  "Harmonic or inharmonic: string, bell, glass, chord"),
            makeKnob(ID::ringsBrightness, "Brightness", Palette::kJade, KnobTier::Hero, Dest::RingsBrightness, "How much high light the body keeps"),
            makeKnob(ID::ringsDamping,    "Damping",    Palette::kJade, KnobTier::Hero, Dest::RingsDamping,    "How long the body goes on ringing"),
            makeKnob(ID::ringsPosition,   "Position",   Palette::kJade.withSaturation(0.35f), KnobTier::Medium, Dest::RingsPosition, "Where along the body it is struck"),
            makeKnob(ID::ringsTune,       "Tune",       Palette::kJade.withSaturation(0.35f), KnobTier::Medium, Dest::RingsTune,     "Tune the body against the note played"),
        };

        // ---- III. The sky ------------------------------------------------------
        setupPower(cloudsPower_, ID::cloudsBypass, cloudsPowerAttach_);
        freezeBtn_.setClickingTogglesState(true);
        freezeBtn_.setColour(juce::TextButton::buttonOnColourId, Palette::kPearl);
        freezeBtn_.setTooltip("Hold the last seconds still, and keep playing inside them");
        addAndMakeVisible(freezeBtn_);
        freezeAttach_ = std::make_unique<APVTS::ButtonAttachment>(apvts, ID::cloudsFreeze, freezeBtn_);
        setupCombo(cloudModeCombo_, kCloudsModeNames, ID::cloudsMode, cloudModeAttach_, Palette::kPearl);
        cloudModeCombo_.setTooltip("How the sky remembers");

        stageKnobs_[2] = {
            makeKnob(ID::cloudsPosition, "Position", Palette::kPearl, KnobTier::Hero, Dest::CloudsPosition, "How far back in time the grains listen (or drag across the sky above)"),
            makeKnob(ID::cloudsSize,     "Size",     Palette::kPearl, KnobTier::Hero, Dest::CloudsSize,     "The length of each grain"),
            makeKnob(ID::cloudsDensity,  "Density",  Palette::kPearl, KnobTier::Hero, Dest::CloudsDensity,  "Sparse and regular below the middle, dense and scattered above"),
            makeKnob(ID::cloudsTexture,  "Texture",  Palette::kPearl, KnobTier::Hero, Dest::CloudsTexture,  "The softness of each grain, and the diffusion"),
            makeKnob(ID::cloudsPitch,    "Pitch",    Palette::kIris, KnobTier::Medium, Dest::CloudsPitch,    "Transpose the cloud, in semitones"),
            makeKnob(ID::cloudsFeedback, "Feedback", Palette::kIris, KnobTier::Medium, Dest::CloudsFeedback, "Let the cloud feed on itself"),
            makeKnob(ID::cloudsReverb,   "Reverb",   Palette::kIris, KnobTier::Medium, Dest::CloudsReverb,   "The size of the space around it all"),
            makeKnob(ID::cloudsDryWet,   "Mix",      Palette::kIris, KnobTier::Medium, Dest::CloudsDryWet,   "From the bare body to the whole sky"),
            makeKnob(ID::cloudsSpread,   "Width",    Palette::kIris, KnobTier::Medium, std::nullopt,          "How wide the cloud spreads"),
        };

        // ---- Tides --------------------------------------------------------------
        tides_.onAssignModeChanged = [this](std::optional<ric::mod::Source> src) {
            // choosing a tide returns the routing column to "where it flows"
            if (inspectedKnob_ != nullptr) { inspectedKnob_->setInspected(false); inspectedKnob_ = nullptr; }
            for (auto* k : allKnobs_) k->setActiveModAssignSource(src);
            touchSource_ = src;
            repaint();
        };
        tides_.onRoutingChanged = [this]() { refreshAllBadges(); };
        addAndMakeVisible(tides_);
        for (auto* k : tides_.getKnobs()) allKnobs_.push_back(k);

        for (auto* k : allKnobs_) {
            k->onTideDropped = [this](ric::mod::Source src) {
                tides_.select(static_cast<int>(src));
                refreshAllBadges();
            };
            k->onFocused = [this, k](ric::mod::Dest d) {
                if (inspectedKnob_ != nullptr) inspectedKnob_->setInspected(false);
                inspectedKnob_ = k;
                k->setInspected(true);
                tides_.focusDestination(d);
            };
        }

        addChildComponent(presetBrowser_);
        addChildComponent(aboutOverlay_);

        onPresetChanged();
        startTimerHz(60);
    }

    ~MainView() override { stopTimer(); }

    //==========================================================================
    void paint(juce::Graphics& g) override {
        juce::ColourGradient bg(Palette::kDusk, 0.0f, 0.0f, Palette::kNight, 0.0f, static_cast<float>(getHeight()), false);
        g.setGradientFill(bg);
        g.fillAll();

        paintHeader(g);

        struct StageText { const char* numeral; const char* name; const char* poem; juce::Colour c; };
        const StageText stages[3] = {
            { "I",   "VOICE & EXCITER", "what is sung into the body",        Palette::kEmber },
            { "II",  "RESONATOR",       "what holds the sound, and rings",   Palette::kJade },
            { "III", "CLOUDS & SPACE",  "where the sound drifts, and remembers", Palette::kPearl },
        };
        const bool bypassed[3] = { exciterPower_.getToggleState(), ringsPower_.getToggleState(), cloudsPower_.getToggleState() };
        for (int i = 0; i < 3; ++i) {
            const auto r = stageBounds(i).toFloat();
            Draw::card(g, r, 16.0f, bypassed[i] ? 0.6f : 1.0f);
            if (touchSource_.has_value()) {
                g.setColour(Palette::kRose.withAlpha(0.65f));
                g.drawRoundedRectangle(r.reduced(0.5f), 16.0f, 1.6f);
            }
            const auto& s = stages[i];
            const auto col = bypassed[i] ? Palette::kAsh : s.c;
            g.setColour(col);
            g.fillRoundedRectangle(r.getX() + 18.0f, r.getY() + 1.0f, 36.0f, 2.0f, 1.0f);
            g.setFont(Fonts::serif(15.0f));
            g.drawText(s.numeral, juce::Rectangle<float>(r.getX() + 18.0f, r.getY() + 12.0f, 34.0f, 20.0f), juce::Justification::centredLeft);
            g.setFont(Fonts::caps(10.5f, true));
            g.setColour(bypassed[i] ? Palette::kFaint : Palette::kMoon);
            g.drawText(s.name, juce::Rectangle<float>(r.getX() + 50.0f, r.getY() + 8.0f, 220.0f, 16.0f), juce::Justification::centredLeft);
            g.setFont(Fonts::serif(12.5f, true));
            g.setColour(Palette::kMist.withAlpha(bypassed[i] ? 0.5f : 0.9f));
            g.drawText(bypassed[i] ? juce::String("resting - the signal passes around it") : juce::String(s.poem),
                       juce::Rectangle<float>(r.getX() + 50.0f, r.getY() + 23.0f, 260.0f, 16.0f), juce::Justification::centredLeft);
        }

        // Signal flow between the stages: a single mote of light in each gap
        for (int i = 0; i < 2; ++i) {
            const auto a = stageBounds(i);
            const float x = (static_cast<float>(a.getRight()) + static_cast<float>(stageBounds(i + 1).getX())) * 0.5f;
            const float y = static_cast<float>(a.getCentreY());
            Draw::glow(g, { x, y }, 8.0f, Palette::kGold, 0.6f);
            g.setColour(Palette::kGold);
            g.fillEllipse(x - 1.5f, y - 1.5f, 3.0f, 3.0f);
        }
    }

    void resized() override {
        // Header
        prevPresetBtn_.setBounds(432, 18, 30, 30);
        presetTitle_.setBounds(466, 8, 348, 50);
        nextPresetBtn_.setBounds(818, 18, 30, 30);

        polyphonyCombo_.setBounds(902, 19, 120, 28);
        undoBtn_.setBounds(1030, 19, 28, 28);
        redoBtn_.setBounds(1060, 19, 28, 28);
        learnBtn_.setBounds(1096, 20, 62, 26);
        aboutBtn_.setBounds(1164, 19, 28, 28);
        masterKnob_->setBounds(1214, 4, 52, 60);

        canvas_.setBounds(canvasBounds());
        const auto s1 = stageBounds(0), s2 = stageBounds(1), s3 = stageBounds(2);
        canvas_.setZoneSplits(static_cast<float>((s1.getRight() + s2.getX()) / 2 - canvasBounds().getX()),
                              static_cast<float>((s2.getRight() + s3.getX()) / 2 - canvasBounds().getX()));

        // I.
        exciterPower_.setBounds(s1.getRight() - 44, s1.getY() + 10, 28, 28);
        {
            auto row = comboRow(s1);
            sourceCombo_.setBounds(row.removeFromRight(124));
            row.removeFromRight(8);
            engineCombo_.setBounds(row);
        }
        layoutKnobs(s1, stageKnobs_[0], 3);

        // II.
        ringsPower_.setBounds(s2.getRight() - 44, s2.getY() + 10, 28, 28);
        strumBtn_.setBounds(s2.getRight() - 44 - 8 - 70, s2.getY() + 13, 70, 22);
        {
            auto row = comboRow(s2);
            stringsCombo_.setBounds(row.removeFromRight(104));
            row.removeFromRight(8);
            modelCombo_.setBounds(row);
        }
        layoutKnobs(s2, stageKnobs_[1], 3);

        // III.
        cloudsPower_.setBounds(s3.getRight() - 44, s3.getY() + 10, 28, 28);
        freezeBtn_.setBounds(s3.getRight() - 44 - 8 - 74, s3.getY() + 13, 74, 22);
        cloudModeCombo_.setBounds(comboRow(s3));
        layoutKnobs(s3, stageKnobs_[2], 4);

        tides_.setBounds(tidesBounds());

        presetBrowser_.setBounds(getLocalBounds());
        aboutOverlay_.setBounds(getLocalBounds());
    }

private:
    static juce::Rectangle<int> canvasBounds() { return { 16, 70, 1248, 252 }; }
    static juce::Rectangle<int> tidesBounds() { return { 16, 606, 1248, 182 }; }

    static juce::Rectangle<int> stageBounds(int i) {
        static constexpr int xs[3] = { 16, 400, 832 };
        static constexpr int ws[3] = { 372, 420, 432 };
        return { xs[i], 330, ws[i], 268 };
    }

    static juce::Rectangle<int> comboRow(juce::Rectangle<int> s) {
        return { s.getX() + 16, s.getY() + 50, s.getWidth() - 32, 28 };
    }

    // first `heroCount` knobs on the hero row, the rest on the row beneath
    static void layoutKnobs(juce::Rectangle<int> s, const std::vector<PoeticKnob*>& knobs, size_t heroCount) {
        auto heroRow = juce::Rectangle<int>(s.getX() + 10, s.getY() + 86, s.getWidth() - 20, 104);
        auto smallRow = juce::Rectangle<int>(s.getX() + 10, s.getY() + 194, s.getWidth() - 20, 70);
        const size_t smallCount = knobs.size() - heroCount;
        const int hw = heroRow.getWidth() / static_cast<int>(heroCount);
        const int sw = smallCount > 0 ? std::min(hw, smallRow.getWidth() / static_cast<int>(smallCount)) : 0;
        smallRow = smallRow.withSizeKeepingCentre(sw * static_cast<int>(smallCount), smallRow.getHeight());
        for (size_t i = 0; i < knobs.size(); ++i) {
            if (i < heroCount) knobs[i]->setBounds(heroRow.removeFromLeft(hw));
            else               knobs[i]->setBounds(smallRow.removeFromLeft(sw));
        }
    }

    void paintHeader(juce::Graphics& g) {
        // Emblem: a ring giving itself to a cloud
        const juce::Point<float> em(40.0f, 34.0f);
        Draw::glow(g, em, 22.0f, Palette::kJade, 0.4f);
        g.setColour(Palette::kJade);
        g.drawEllipse(em.x - 11.0f, em.y - 11.0f, 22.0f, 22.0f, 1.5f);
        juce::Random rng(11);
        for (int i = 0; i < 9; ++i) {
            const float t = static_cast<float>(i) / 9.0f;
            const float x = em.x + 13.0f + t * 16.0f;
            const float y = em.y - 6.0f + rng.nextFloat() * 12.0f;
            g.setColour(Palette::kPearl.withAlpha(0.9f - t * 0.6f));
            g.fillEllipse(x - 1.2f, y - 1.2f, 2.4f, 2.4f);
        }

        g.setColour(Palette::kMoon);
        g.setFont(Fonts::serif(25.0f, true));
        g.drawText("Kings into Clowns", juce::Rectangle<float>(80.0f, 10.0f, 300.0f, 30.0f), juce::Justification::centredLeft);
        g.setFont(Fonts::caps(8.5f));
        g.setColour(Palette::kFaint);
        g.drawText("A RESONATOR & CLOUD INSTRUMENT  -  MTYAS", juce::Rectangle<float>(81.0f, 40.0f, 320.0f, 14.0f), juce::Justification::centredLeft);

        // Output light
        const auto meter = juce::Rectangle<float>(1200.0f, 17.0f, 8.0f, 32.0f);
        for (int ch = 0; ch < 2; ++ch) {
            const auto bar = meter.withWidth(3.0f).translated(static_cast<float>(ch) * 4.5f, 0.0f);
            g.setColour(Palette::kWell);
            g.fillRoundedRectangle(bar, 1.5f);
            const float lvl = std::clamp(ch == 0 ? meterL_ : meterR_, 0.0f, 1.0f);
            const float dbNorm = lvl > 0.0001f ? std::clamp((juce::Decibels::gainToDecibels(lvl) + 48.0f) / 48.0f, 0.0f, 1.0f) : 0.0f;
            const auto fill = bar.withTrimmedTop(bar.getHeight() * (1.0f - dbNorm));
            g.setColour((lvl > 0.95f ? Palette::kCoral : Palette::kJade).withAlpha(0.9f));
            g.fillRoundedRectangle(fill, 1.5f);
        }
    }

    PoeticKnob* makeKnob(const char* paramId, const char* label, juce::Colour colour, KnobTier tier,
                         std::optional<ric::mod::Dest> dest, const juce::String& tooltip) {
        auto knob = std::make_unique<PoeticKnob>(processor_.getAPVTS(), paramId, label, colour, tier, dest,
                                                 &processor_.getSynthEngine().getModMatrix(),
                                                 &processor_.getMidiLearnManager());
        knob->setTooltipText(tooltip);
        addAndMakeVisible(*knob);
        auto* raw = knob.get();
        if (dest.has_value()) modulatableKnobs_[static_cast<size_t>(*dest)] = raw;
        allKnobs_.push_back(raw);
        ownedKnobs_.push_back(std::move(knob));
        return raw;
    }

    void setupCombo(juce::ComboBox& box, const juce::StringArray& items, const char* paramId,
                    std::unique_ptr<APVTS::ComboBoxAttachment>& attach, juce::Colour accent) {
        box.addItemList(items, 1);
        box.setColour(juce::ComboBox::focusedOutlineColourId, accent);
        addAndMakeVisible(box);
        attach = std::make_unique<APVTS::ComboBoxAttachment>(processor_.getAPVTS(), paramId, box);
    }

    void setupPower(StagePowerButton& b, const char* paramId, std::unique_ptr<APVTS::ButtonAttachment>& attach) {
        b.onStateChange = [this]() { repaint(); };
        addAndMakeVisible(b);
        attach = std::make_unique<APVTS::ButtonAttachment>(processor_.getAPVTS(), paramId, b);
    }

    void onPresetChanged() {
        auto& pm = processor_.getPresetManager();
        if (const auto* cur = pm.getCurrentPreset()) presetTitle_.setPreset(cur->name, cur->poemLine);
        else presetTitle_.setPreset("Untitled", "");
        lastPresetIndex_ = pm.getCurrentIndex();
        tides_.refreshRouting();
        refreshAllBadges();
    }

    void refreshAllBadges() {
        for (auto* k : allKnobs_) k->refreshVisualBadges();
        lastMatrixVersion_ = processor_.getSynthEngine().getModMatrix().getVersion();
    }

    void timerCallback() override {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = lastFrameMs_ > 0.0 ? static_cast<float>(std::min(0.1, (now - lastFrameMs_) * 0.001)) : 1.0f / 60.0f;
        lastFrameMs_ = now;

        if (processor_.getVisualFifo().popLatest(snapshot_)) {
            for (size_t d = 0; d < modulatableKnobs_.size(); ++d)
                if (modulatableKnobs_[d] != nullptr)
                    modulatableKnobs_[d]->setModulatedNormalizedValue(snapshot_.modulatedNormalized[d]);
            tides_.advance(snapshot_);
            meterL_ = std::max(snapshot_.peakLeft, meterL_ * 0.86f);
            meterR_ = std::max(snapshot_.peakRight, meterR_ * 0.86f);
        } else {
            meterL_ *= 0.86f;
            meterR_ *= 0.86f;
        }
        canvas_.advance(snapshot_, dt);
        repaint(1196, 14, 16, 38);

        applyKnobRoles();

        // Touch mode: every dial the tide can reach breathes in rose
        if (touchSource_.has_value()) {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now * 0.0045));
            for (auto* k : modulatableKnobs_)
                if (k != nullptr) k->setAssignPulse(pulse);
        }

        // Dim the dials of a resting stage
        const bool bypassed[3] = { exciterPower_.getToggleState(), ringsPower_.getToggleState(), cloudsPower_.getToggleState() };
        for (int i = 0; i < 3; ++i)
            for (auto* k : stageKnobs_[static_cast<size_t>(i)]) k->setDimmed(bypassed[i]);

        // Host program changes, routing edits made elsewhere, MIDI learn finishing
        auto& pm = processor_.getPresetManager();
        if (pm.getCurrentIndex() != lastPresetIndex_) onPresetChanged();
        if (processor_.getSynthEngine().getModMatrix().getVersion() != lastMatrixVersion_) refreshAllBadges();
        const bool learnNow = processor_.getMidiLearnManager().isLearnActive();
        if (learnBtn_.getToggleState() != learnNow) {
            learnBtn_.setToggleState(learnNow, juce::dontSendNotification);
            refreshAllBadges();
        }

        // Group each gesture into its own undo step
        if (!juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            processor_.getUndoManager().beginNewTransaction();
        undoBtn_.setEnabled(processor_.getUndoManager().canUndo());
        redoBtn_.setEnabled(processor_.getUndoManager().canRedo());
    }

    KingsIntoClownsProcessor& processor_;

    IconButton prevPresetBtn_{ IconButton::Icon::Left, "Previous patch" };
    IconButton nextPresetBtn_{ IconButton::Icon::Right, "Next patch" };
    PresetTitle presetTitle_;
    juce::ComboBox polyphonyCombo_;
    std::unique_ptr<APVTS::ComboBoxAttachment> polyphonyAttach_;
    IconButton undoBtn_{ IconButton::Icon::Undo, "Undo" };
    IconButton redoBtn_{ IconButton::Icon::Redo, "Redo" };
    juce::TextButton learnBtn_{ "Learn" };
    IconButton aboutBtn_{ IconButton::Icon::Info, "About" };
    PoeticKnob* masterKnob_ = nullptr;

    JourneyCanvas canvas_;

    StagePowerButton exciterPower_{ Palette::kEmber };
    std::unique_ptr<APVTS::ButtonAttachment> exciterPowerAttach_;
    juce::ComboBox engineCombo_, sourceCombo_;
    std::unique_ptr<APVTS::ComboBoxAttachment> engineAttach_, sourceAttach_;

    StagePowerButton ringsPower_{ Palette::kJade };
    std::unique_ptr<APVTS::ButtonAttachment> ringsPowerAttach_;
    juce::TextButton strumBtn_{ "Strum" };
    juce::ComboBox modelCombo_, stringsCombo_;
    std::unique_ptr<APVTS::ComboBoxAttachment> modelAttach_, stringsAttach_;

    StagePowerButton cloudsPower_{ Palette::kPearl };
    std::unique_ptr<APVTS::ButtonAttachment> cloudsPowerAttach_;
    juce::TextButton freezeBtn_{ "Freeze" };
    std::unique_ptr<APVTS::ButtonAttachment> freezeAttach_;
    juce::ComboBox cloudModeCombo_;
    std::unique_ptr<APVTS::ComboBoxAttachment> cloudModeAttach_;

    std::vector<std::unique_ptr<PoeticKnob>> ownedKnobs_;
    std::array<std::vector<PoeticKnob*>, 3> stageKnobs_;
    std::array<PoeticKnob*, ric::mod::ModMatrix::kNumDests> modulatableKnobs_{};
    std::vector<PoeticKnob*> allKnobs_;
    PoeticKnob* inspectedKnob_ = nullptr;
    std::optional<ric::mod::Source> touchSource_;
    std::array<int, 4> lastRoleKeys_ { -1, -1, -1, -1 };

    // Names and tooltips follow the selected voice model, resonator model and cloud mode
    void applyKnobRoles() {
        auto& apvts = processor_.getAPVTS();
        auto read = [&apvts](const char* id) { return static_cast<int>(apvts.getRawParameterValue(id)->load()); };
        const std::array<int, 4> keys { read(ric::params::ID::oscEngine), read(ric::params::ID::ringsModel),
                                        read(ric::params::ID::cloudsMode), read(ric::params::ID::inputSource) };
        if (keys == lastRoleKeys_) return;
        lastRoleKeys_ = keys;

        const auto& voice = Roles::kVoice[static_cast<size_t>(std::clamp(keys[0], 0, 11))];
        const bool outerOnly = keys[3] == 1; // only the track's audio: the inner voice is silent
        for (size_t i = 0; i < voice.size(); ++i) {
            if (outerOnly) stageKnobs_[0][i]->setRole(voice[i].label, "The inner voice is silent while the source is outer audio", false);
            else           stageKnobs_[0][i]->setRole(voice[i].label, voice[i].tooltip, voice[i].used);
        }
        stageKnobs_[0][3]->setRole("Tune", outerOnly ? "The inner voice is silent while the source is outer audio"
                                                     : "Transpose the voice, in semitones", !outerOnly);
        stageKnobs_[0][4]->setRole("Sub", outerOnly ? "The inner voice is silent while the source is outer audio"
                                                    : "A lower shadow beneath the voice", !outerOnly);

        const auto& body = Roles::kBody[static_cast<size_t>(std::clamp(keys[1], 0, 5))];
        for (size_t i = 0; i < body.size(); ++i) stageKnobs_[1][i]->setRole(body[i].label, body[i].tooltip, body[i].used);

        const auto& sky = Roles::kSky[static_cast<size_t>(std::clamp(keys[2], 0, 3))];
        for (size_t i = 0; i < sky.size(); ++i) stageKnobs_[2][i]->setRole(sky[i].label, sky[i].tooltip, sky[i].used);
    }

    TidesPanel tides_;
    PresetBrowserModal presetBrowser_;
    AboutOverlay aboutOverlay_;
    juce::TooltipWindow tooltipWindow_{ this, 650 };

    ric::core::VisualSnapshot snapshot_{};
    double lastFrameMs_ = 0.0;
    float meterL_ = 0.0f, meterR_ = 0.0f;
    int lastPresetIndex_ = -1;
    uint32_t lastMatrixVersion_ = 0;
};

} // namespace ric::ui

//==============================================================================
KingsIntoClownsEditor::KingsIntoClownsEditor(KingsIntoClownsProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    setLookAndFeel(&lookAndFeel_);
    view_ = std::make_unique<ric::ui::MainView>(p);
    addAndMakeVisible(*view_);

    setResizable(true, true);
    setResizeLimits(kDesignWidth * 3 / 4, kDesignHeight * 3 / 4, kDesignWidth * 3 / 2, kDesignHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(static_cast<double>(kDesignWidth) / kDesignHeight);

    const int saved = processor_.getSavedEditorWidth();
    const int w = saved > 0 ? std::clamp(saved, kDesignWidth * 3 / 4, kDesignWidth * 3 / 2) : kDesignWidth;
    setSize(w, w * kDesignHeight / kDesignWidth);
}

KingsIntoClownsEditor::~KingsIntoClownsEditor() {
    view_.reset();
    setLookAndFeel(nullptr);
}

void KingsIntoClownsEditor::paint(juce::Graphics& g) {
    g.fillAll(ric::ui::Palette::kNight);
}

void KingsIntoClownsEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(kDesignWidth);
    view_->setBounds(0, 0, kDesignWidth, kDesignHeight);
    view_->setTransform(juce::AffineTransform::scale(scale));
    processor_.setSavedEditorWidth(getWidth());
}
