#include "../Source/DSP/SynthEngine.h"
#include "../Source/Core/LockFreeVisualFifo.h"
#include "../Source/Core/MidiLearnManager.h"
#include <iostream>
#include <cmath>
#include <cassert>

namespace {

bool verifyBufferFiniteAndActive(const juce::AudioBuffer<float>& buf, float& outRms) {
    double sumSq = 0.0;
    const int chs = buf.getNumChannels();
    const int n = buf.getNumSamples();
    for (int c = 0; c < chs; ++c) {
        const float* p = buf.getReadPointer(c);
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(p[i])) return false;
            sumSq += static_cast<double>(p[i]) * static_cast<double>(p[i]);
        }
    }
    outRms = static_cast<float>(std::sqrt(sumSq / std::max(1, chs * n)));
    return true;
}

ric::dsp::EngineParameters makeDefaultTestParams() {
    using namespace ric::mod;
    ric::dsp::EngineParameters ep{};
    ep.maxPolyphony = 8;
    ep.masterGainLinear = 1.0f;
    ep.voiceCtx.exciterBypass = false;
    ep.voiceCtx.inputSource = 0;
    ep.voiceCtx.ringsBypass = false;
    ep.voiceCtx.cloudsBypass = false;
    ep.voiceCtx.oscEngine = 0;
    ep.voiceCtx.ringsModel = 0;
    ep.voiceCtx.ringsPoly = 1;

    for (size_t i = 0; i < 3; ++i) {
        ep.voiceCtx.lfoRates[i] = 1.5f + static_cast<float>(i);
        ep.voiceCtx.lfoShapes[i] = static_cast<int>(i);
        ep.voiceCtx.envs[i].attack = 0.005f;
        ep.voiceCtx.envs[i].decay = 1.2f;
        ep.voiceCtx.envs[i].sustain = 0.6f;
        ep.voiceCtx.envs[i].release = 1.5f;
    }

    auto& b = ep.voiceCtx.baseDestParams;
    b[static_cast<size_t>(Dest::OscTune)]         = 0.0f;
    b[static_cast<size_t>(Dest::OscHarmonics)]    = 0.5f;
    b[static_cast<size_t>(Dest::OscTimbre)]       = 0.5f;
    b[static_cast<size_t>(Dest::OscMorph)]        = 0.5f;
    b[static_cast<size_t>(Dest::SubBlend)]        = 0.35f;
    b[static_cast<size_t>(Dest::StrikeLevel)]     = 0.65f;
    b[static_cast<size_t>(Dest::DirectMix)]       = 0.2f;
    b[static_cast<size_t>(Dest::RingsStructure)]  = 0.4f;
    b[static_cast<size_t>(Dest::RingsBrightness)] = 0.7f;
    b[static_cast<size_t>(Dest::RingsDamping)]    = 0.8f;
    b[static_cast<size_t>(Dest::RingsPosition)]   = 0.35f;
    b[static_cast<size_t>(Dest::RingsTune)]       = 0.0f;
    b[static_cast<size_t>(Dest::CloudsPosition)]  = 0.35f;
    b[static_cast<size_t>(Dest::CloudsSize)]      = 0.6f;
    b[static_cast<size_t>(Dest::CloudsDensity)]   = 0.72f;
    b[static_cast<size_t>(Dest::CloudsTexture)]   = 0.65f;
    b[static_cast<size_t>(Dest::CloudsPitch)]     = 0.0f;
    b[static_cast<size_t>(Dest::CloudsFeedback)]  = 0.35f;
    b[static_cast<size_t>(Dest::CloudsReverb)]    = 0.6f;
    b[static_cast<size_t>(Dest::CloudsDryWet)]    = 0.45f;

    ep.clouds.mode = 0;
    ep.clouds.freeze = false;
    ep.clouds.position = 0.35f;
    ep.clouds.size = 0.6f;
    ep.clouds.pitch = 0.0f;
    ep.clouds.density = 0.72f;
    ep.clouds.texture = 0.65f;
    ep.clouds.feedback = 0.35f;
    ep.clouds.reverb = 0.6f;
    ep.clouds.stereoSpread = 0.8f;
    ep.clouds.dryWet = 0.45f;

    return ep;
}

} // namespace

int main() {
    std::cout << "=== Kings into Clowns (mtyas) Unified Instrument Verification Suite ===" << std::endl;

    ric::dsp::SynthEngine engine;
    ric::core::LockFreeVisualFifo fifo;
    engine.prepare(48000.0, 512);

    juce::AudioBuffer<float> audioBuf(2, 512);
    juce::MidiBuffer midiBuf;

    // 1. Verify all 12 Plaits/Sub Macro Oscillator Engines
    for (int oscIdx = 0; oscIdx < 12; ++oscIdx) {
        engine.reset();
        auto params = makeDefaultTestParams();
        params.voiceCtx.oscEngine = oscIdx;
        params.voiceCtx.ringsBypass = true;
        params.voiceCtx.cloudsBypass = true;

        midiBuf.clear();
        midiBuf.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

        float rms = 0.0f;
        engine.renderBlock(audioBuf, midiBuf, params, fifo);
        if (!verifyBufferFiniteAndActive(audioBuf, rms) || rms < 0.0001f) {
            std::cerr << "[FAIL] Oscillator Engine " << oscIdx << " failed RMS/finite check (RMS=" << rms << ")" << std::endl;
            return 1;
        }
        std::cout << "[PASS] Macro Oscillator Engine #" << oscIdx << " RMS = " << rms << std::endl;
    }

    // 2. Verify all 6 Rings Physical Modeling Modes (including Exciter Bypass + Direct MIDI Pitch/Mallet)
    for (int ringsIdx = 0; ringsIdx < 6; ++ringsIdx) {
        engine.reset();
        auto params = makeDefaultTestParams();
        params.voiceCtx.ringsModel = ringsIdx;
        params.voiceCtx.exciterBypass = true;
        params.voiceCtx.cloudsBypass = true;

        midiBuf.clear();
        midiBuf.addEvent(juce::MidiMessage::noteOn(1, 64, 0.85f), 0);

        float rms = 0.0f;
        engine.renderBlock(audioBuf, midiBuf, params, fifo);
        if (!verifyBufferFiniteAndActive(audioBuf, rms) || rms < 0.0001f) {
            std::cerr << "[FAIL] Rings Model " << ringsIdx << " failed RMS/finite check (RMS=" << rms << ")" << std::endl;
            return 1;
        }
        // Sustained level over the next ~200 ms, so models can be balanced against each other
        midiBuf.clear();
        double sustainedSq = 0.0;
        float peak = audioBuf.getMagnitude(0, audioBuf.getNumSamples());
        for (int blk = 0; blk < 20; ++blk) {
            engine.renderBlock(audioBuf, midiBuf, params, fifo);
            float blkRms = 0.0f;
            verifyBufferFiniteAndActive(audioBuf, blkRms);
            sustainedSq += static_cast<double>(blkRms) * blkRms;
            peak = std::max(peak, audioBuf.getMagnitude(0, audioBuf.getNumSamples()));
        }
        std::cout << "[PASS] Rings Physical Model #" << ringsIdx << " (Direct MIDI Pitch/Mallet) RMS = " << rms
                  << "  sustained RMS = " << std::sqrt(sustainedSq / 20.0) << "  peak = " << peak << std::endl;
    }

    // 3. Verify External Audio Input FX Mode (Audio In -> Rings -> Clouds without MIDI notes)
    {
        engine.reset();
        auto params = makeDefaultTestParams();
        params.voiceCtx.inputSource = 1;
        params.voiceCtx.exciterBypass = true;
        midiBuf.clear();

        for (int i = 0; i < 512; ++i) {
            const float s = std::sin(static_cast<float>(i) * 0.1f) * 0.5f;
            audioBuf.setSample(0, i, s);
            audioBuf.setSample(1, i, s);
        }

        float rms = 0.0f;
        engine.renderBlock(audioBuf, midiBuf, params, fifo);
        if (!verifyBufferFiniteAndActive(audioBuf, rms) || rms < 0.0001f) {
            std::cerr << "[FAIL] External Audio Input FX Mode failed (RMS=" << rms << ")" << std::endl;
            return 1;
        }
        std::cout << "[PASS] External Audio Input FX Mode (Audio In -> Rings -> Clouds) RMS = " << rms << std::endl;

        // The drone voice must keep listening steadily (it used to switch itself off every few blocks)
        float minRms = 1.0f;
        for (int blk = 0; blk < 60; ++blk) {
            for (int i = 0; i < 512; ++i) {
                const float s = std::sin(static_cast<float>(blk * 512 + i) * 0.1f) * 0.5f;
                audioBuf.setSample(0, i, s);
                audioBuf.setSample(1, i, s);
            }
            engine.renderBlock(audioBuf, midiBuf, params, fifo);
            float blkRms = 0.0f;
            verifyBufferFiniteAndActive(audioBuf, blkRms);
            if (blk >= 4) minRms = std::min(minRms, blkRms);
        }
        if (minRms < 0.005f) {
            std::cerr << "[FAIL] External Audio drone dropped out (min block RMS=" << minRms << ")" << std::endl;
            return 1;
        }
        std::cout << "[PASS] External Audio drone stays steady (min block RMS = " << minRms << ")" << std::endl;
    }

    // 4. Verify 3 LFOs + 3 ADSRs + Loom + Real-Time Modulated Ring Telemetry
    {
        engine.reset();
        auto params = makeDefaultTestParams();
        params.marbles.enabled = true;
        params.marbles.tempoSync = true;
        params.marbles.divisionIndex = 7;
        params.marbles.hostBpm = 140.0;
        midiBuf.clear();

        engine.getModMatrix().assignOrUpdateConnection(ric::mod::Source::LFO1, ric::mod::Dest::RingsStructure, 0.50f);
        engine.getModMatrix().assignOrUpdateConnection(ric::mod::Source::ENV2, ric::mod::Dest::OscTimbre, 0.45f);
        engine.getModMatrix().assignOrUpdateConnection(ric::mod::Source::LOOM, ric::mod::Dest::CloudsPosition, 0.40f);
        engine.handleNoteOn(60, 0.85f, 0, 1001);

        float rms = 0.0f;
        for (int b = 0; b < 6; ++b) {
            engine.renderBlock(audioBuf, midiBuf, params, fifo);
        }
        ric::core::VisualSnapshot snap{};
        const bool gotSnap = fifo.popLatest(snap);
        if (!verifyBufferFiniteAndActive(audioBuf, rms) || rms < 0.0001f || !gotSnap) {
            std::cerr << "[FAIL] 3 LFO + 3 ADSR + Loom + Visual Ring Telemetry test failed" << std::endl;
            return 1;
        }
        std::cout << "[PASS] 3 LFOs + 3 ADSRs + Loom + Live Ring Telemetry (RingsStructure modNorm="
                  << snap.modulatedNormalized[static_cast<size_t>(ric::mod::Dest::RingsStructure)]
                  << ") RMS = " << rms << std::endl;
    }

    // 5. Loom melody target: "Rhythm only" keeps the pitch, "Voice & Body" moves it
    for (int target : { 0, 1 }) {
        engine.reset();
        auto params = makeDefaultTestParams();
        params.marbles.enabled = true;
        params.marbles.tempoSync = false;
        params.marbles.rateHz = 12.0f;
        params.marbles.spread = 0.9f;
        params.marbles.dejaVu = 0.0f;
        params.voiceCtx.loomPitchTarget = target;
        midiBuf.clear();
        float minNote = 999.0f, maxNote = -999.0f;
        for (int blk = 0; blk < 200; ++blk) {
            engine.renderBlock(audioBuf, midiBuf, params, fifo);
            ric::core::VisualSnapshot snap{};
            if (fifo.popLatest(snap))
                for (const auto& v : snap.voices)
                    if (v.active) { minNote = std::min(minNote, v.note); maxNote = std::max(maxNote, v.note); }
        }
        const bool moved = maxNote - minNote > 0.5f;
        if (maxNote < 0.0f || moved != (target == 1)) {
            std::cerr << "[FAIL] Loom melody target " << target << " (notes " << minNote << ".." << maxNote << ")" << std::endl;
            return 1;
        }
        std::cout << "[PASS] Loom melody target " << target << " (voice notes " << minNote << " .. " << maxNote << ")" << std::endl;
    }

    std::cout << "=== ALL DSP VERIFICATION TESTS PASSED ===" << std::endl;
    return 0;
}
