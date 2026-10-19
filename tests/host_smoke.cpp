// Dev tool: hosts the built VST3 like a DAW would (load, play, open the editor in a window,
// close, unload) and reports how long each stage takes - used to catch shutdown hangs.
//   ric_host_smoke "<path to .vst3>"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace {
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
void stage(const char* name, double t0) { std::cout << "[" << static_cast<int>(nowMs() - t0) << " ms] " << name << std::endl; }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: ric_host_smoke <plugin.vst3>" << std::endl; return 2; }
    const double t0 = nowMs();
    {
        juce::ScopedJuceInitialiser_GUI init;
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile(types, juce::String(argv[1]));
        if (types.isEmpty()) { std::cerr << "no plugin found" << std::endl; return 3; }
        stage("scanned", t0);

        juce::String error;
        auto instance = format.createInstanceFromDescription(*types[0], 48000.0, 512, error);
        if (instance == nullptr) { std::cerr << "load failed: " << error << std::endl; return 4; }
        instance->setPlayConfigDetails(2, 2, 48000.0, 512);
        instance->prepareToPlay(48000.0, 512);
        stage("instantiated", t0);

        auto* editor = instance->createEditorIfNeeded();
        auto window = std::make_unique<juce::DocumentWindow>("host smoke", juce::Colours::black, juce::DocumentWindow::closeButton);
        window->setContentNonOwned(editor, true);
        window->setUsingNativeTitleBar(true);
        window->setVisible(true);
        stage("editor open", t0);

        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 160; ++b) {
            midi.clear();
            if (b == 0) for (int n : { 48, 55, 64 }) midi.addEvent(juce::MidiMessage::noteOn(1, n, 0.8f), 0);
            buf.clear();
            instance->processBlock(buf, midi);
            if (b % 4 == 3) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
        stage("played with editor visible", t0);

        window->clearContentComponent();
        delete editor;
        window.reset();
        stage("editor closed", t0);

        instance->releaseResources();
        instance.reset();
        stage("plugin deleted", t0);
    }
    stage("juce shut down", t0);
    return 0;
}
