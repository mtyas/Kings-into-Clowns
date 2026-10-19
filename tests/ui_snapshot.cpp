// Dev tool: renders the editor off-screen while the engine plays, and saves PNG snapshots.
//   ric_ui_snapshot <output-folder>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TidesPanel.h"
#include "UI/PresetBrowserModal.h"
#include "UI/JourneyCanvas.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

template <typename T>
T* findChild(juce::Component& root) {
    if (auto* t = dynamic_cast<T*>(&root)) return t;
    for (auto* c : root.getChildren())
        if (auto* found = findChild<T>(*c)) return found;
    return nullptr;
}

void setParam(juce::AudioProcessor& p, const juce::String& id, float plainValue) {
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(prm); r != nullptr && r->paramID == id)
            r->setValueNotifyingHost(r->convertTo0to1(plainValue));
}

float lastPeak = 0.0f;

void play(juce::AudioProcessor& proc, double seconds, const std::vector<int>& notes) {
    lastPeak = 0.0f;
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    const int blocks = static_cast<int>(seconds * 48000.0 / 512.0);
    for (int b = 0; b < blocks; ++b) {
        midi.clear();
        if (b == 0)
            for (int n : notes) midi.addEvent(juce::MidiMessage::noteOn(1, n, 0.85f), 0);
        buf.clear();
        proc.processBlock(buf, midi);
        lastPeak = std::max(lastPeak, buf.getMagnitude(0, buf.getNumSamples()));
        if (b % 4 == 3) juce::MessageManager::getInstance()->runDispatchLoopUntil(12);
    }
}

void save(juce::Component& c, const juce::File& file) {
    const auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(img, out);
    std::cout << "wrote " << file.getFullPathName() << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    proc->setPlayConfigDetails(2, 2, 48000.0, 512);
    proc->prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc->createEditor());
    editor->setSize(1280, 800);

    play(*proc, 2.5, { 48, 55, 64 });
    save(*editor, outDir.getChildFile("01_main.png"));

    if (auto* tides = findChild<ric::ui::TidesPanel>(*editor)) tides->select(4);
    play(*proc, 0.6, { 60, 67 });
    save(*editor, outDir.getChildFile("02_envelope.png"));

    setParam(*proc, "marbles_enable", 1.0f);
    setParam(*proc, "clouds_freeze", 1.0f);
    if (auto* tides = findChild<ric::ui::TidesPanel>(*editor)) tides->select(6);
    play(*proc, 2.0, {});
    save(*editor, outDir.getChildFile("03_loom_frozen.png"));

    setParam(*proc, "marbles_enable", 0.0f);
    setParam(*proc, "clouds_freeze", 0.0f);
    setParam(*proc, "rings_bypass", 1.0f);
    if (auto* tides = findChild<ric::ui::TidesPanel>(*editor)) tides->select(0);
    play(*proc, 0.5, { 52 });
    save(*editor, outDir.getChildFile("04_body_resting.png"));
    setParam(*proc, "rings_bypass", 0.0f);

    // A touched dial lists the tides that move it; LFO 2 follows the host tempo
    setParam(*proc, "lfo2_sync", 1.0f);
    setParam(*proc, "lfo2_division", 5.0f);
    if (auto* tides = findChild<ric::ui::TidesPanel>(*editor)) {
        tides->select(1);
    }
    // same path as clicking the Timbre dial
    std::function<void(juce::Component&)> touchTimbre = [&](juce::Component& c) {
        if (auto* k = dynamic_cast<ric::ui::PoeticKnob*>(&c); k != nullptr && k->getParamId() == "osc_timbre" && k->onFocused)
            k->onFocused(ric::mod::Dest::OscTimbre);
        for (auto* child : c.getChildren()) touchTimbre(*child);
    };
    touchTimbre(*editor);
    play(*proc, 0.8, { 55, 62 });
    save(*editor, outDir.getChildFile("06_dial_sources_and_sync.png"));

    {
        // Rough cost of one full repaint of the editor (software renderer)
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 30; ++i) (void) editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        std::cout << "full repaint: " << (juce::Time::getMillisecondCounterHiRes() - t0) / 30.0 << " ms" << std::endl;
        if (auto* canvas = findChild<ric::ui::JourneyCanvas>(*editor)) {
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 60; ++i) (void) canvas->createComponentSnapshot(canvas->getLocalBounds(), true, 1.0f);
            std::cout << "journey canvas repaint: " << (juce::Time::getMillisecondCounterHiRes() - t1) / 60.0 << " ms" << std::endl;
        }
    }

    // Init, then other models / modes: the dial names follow them
    if (auto* kic = dynamic_cast<KingsIntoClownsProcessor*>(proc.get())) kic->getPresetManager().loadInit();
    play(*proc, 1.5, { 48, 55, 64 });
    if (juce::String(argc > 2 ? argv[2] : "") == "--levels") {
        // Level survey: the same chord through every patch
        auto* kic = dynamic_cast<KingsIntoClownsProcessor*>(proc.get());
        auto& pm = kic->getPresetManager();
        auto survey = [&](const juce::String& name) {
            proc->reset();
            play(*proc, 0.05, {});
            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            double sumSq = 0.0; float peak = 0.0f; long n = 0;
            for (int b = 0; b < 200; ++b) {
                midi.clear();
                if (b == 0) for (int note : { 48, 55, 64 }) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.85f), 0);
                if (b == 120) for (int note : { 48, 55, 64 }) midi.addEvent(juce::MidiMessage::noteOff(1, note, 0.0f), 0);
                buf.clear();
                proc->processBlock(buf, midi);
                peak = std::max(peak, buf.getMagnitude(0, 512));
                for (int i = 0; i < 512; ++i) { sumSq += buf.getSample(0, i) * buf.getSample(0, i); ++n; }
            }
            std::cout << "LEVEL  peak " << juce::String(juce::Decibels::gainToDecibels(peak), 1) << " dBFS   rms "
                      << juce::String(juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(sumSq / n))), 1) << " dBFS   " << name << std::endl;
        };
        pm.loadInit();
        survey("Init");
        for (int i = 0; i < pm.getNumPresets(); ++i) { pm.loadPreset(i); survey(pm.getCurrentPreset()->name); }
        return 0;
    }
    std::cout << "Init chord peak: " << juce::Decibels::gainToDecibels(lastPeak) << " dBFS" << std::endl;
    save(*editor, outDir.getChildFile("07_init.png"));
    setParam(*proc, "osc_engine", 2.0f);    // Two-Operator FM
    setParam(*proc, "rings_model", 3.0f);   // FM Follower
    setParam(*proc, "clouds_mode", 1.0f);   // Pitch Shifter
    play(*proc, 1.0, { 57, 64 });
    save(*editor, outDir.getChildFile("08_dynamic_names.png"));

    // Touch mode on
    if (auto* tides = findChild<ric::ui::TidesPanel>(*editor)) tides->select(0);
    std::function<juce::TextButton*(juce::Component&)> findTouch = [&](juce::Component& c) -> juce::TextButton* {
        if (auto* b = dynamic_cast<juce::TextButton*>(&c); b != nullptr && b->getButtonText() == "Touch") return b;
        for (auto* child : c.getChildren()) if (auto* r = findTouch(*child)) return r;
        return nullptr;
    };
    if (auto* touch = findTouch(*editor)) touch->setToggleState(true, juce::sendNotificationSync);
    play(*proc, 0.6, {});
    save(*editor, outDir.getChildFile("09_touch_mode.png"));
    if (auto* touch = findTouch(*editor)) touch->setToggleState(false, juce::sendNotificationSync);

    if (auto* browser = findChild<ric::ui::PresetBrowserModal>(*editor)) browser->openBrowser();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    save(*editor, outDir.getChildFile("05_library.png"));

    editor.reset();
    proc->releaseResources();
    return 0;
}
