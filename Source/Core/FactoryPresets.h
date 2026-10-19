#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include "ParameterDefinitions.h"

namespace ric::presets {

struct FactoryPreset {
    juce::String name;
    juce::String category;
    juce::String poemLine;
    std::map<juce::String, float> values;
    bool isFactory = true;
    juce::File userFile{};
};

inline std::vector<FactoryPreset> getFactoryPresets() {
    using namespace ric::params::ID;
    return {
        {
            "I. Kyoto Rain on Cedar Strings",
            "Plucked & Sympathetic",
            "Sympathetic koto partials dissolving into a frozen courtyard mist.",
            {
                { exciterBypass, 0.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 0.0f },
                { inputSource, 0.0f }, { oscEngine, 9.0f }, { oscTune, 0.0f },
                { oscHarmonics, 0.42f }, { oscTimbre, 0.58f }, { oscMorph, 0.31f },
                { subBlend, 0.25f }, { strikeLevel, 0.65f }, { directMix, 0.12f },
                { ringsModel, 1.0f }, { ringsPoly, 2.0f },
                { ringsStructure, 0.36f }, { ringsBrightness, 0.68f }, { ringsDamping, 0.82f }, { ringsPosition, 0.34f }, { ringsTune, 0.0f },
                { cloudsMode, 0.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.38f }, { cloudsSize, 0.66f }, { cloudsDensity, 0.74f }, { cloudsTexture, 0.68f },
                { cloudsPitch, 0.0f }, { cloudsFeedback, 0.35f }, { cloudsReverb, 0.64f }, { cloudsDryWet, 0.46f },
                { lfo1Rate, 0.28f }, { lfo1Shape, 0.0f }, { lfo2Rate, 0.19f }, { lfo2Shape, 1.0f }, { lfo3Rate, 0.11f }, { lfo3Shape, 0.0f },
                { env1Attack, 0.015f }, { env1Decay, 1.8f }, { env1Sustain, 0.40f }, { env1Release, 3.2f },
                { env2Attack, 0.010f }, { env2Decay, 1.1f }, { env2Sustain, 0.20f }, { env2Release, 1.8f },
                { env3Attack, 0.350f }, { env3Decay, 2.5f }, { env3Sustain, 0.60f }, { env3Release, 4.0f },
                { marblesEnable, 0.0f }, { marblesTempoSync, 1.0f }, { marblesDivision, 4.0f }, { marblesRate, 2.5f },
                { marblesDejaVu, 0.50f }, { marblesSpread, 0.45f }, { marblesSteps, 0.75f },
                { polyphonyMode, 2.0f }, { masterGain, 0.0f }
            },
            true, {}
        },
        {
            "II. Cathedral of Glass & Iron",
            "Modal Bells",
            "Modal bell plates resonating through octave-shifted granular halos.",
            {
                { exciterBypass, 0.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 0.0f },
                { inputSource, 0.0f }, { oscEngine, 2.0f }, { oscTune, 0.0f },
                { oscHarmonics, 0.64f }, { oscTimbre, 0.38f }, { oscMorph, 0.22f },
                { subBlend, 0.35f }, { strikeLevel, 0.80f }, { directMix, 0.18f },
                { ringsModel, 0.0f }, { ringsPoly, 1.0f },
                { ringsStructure, 0.67f }, { ringsBrightness, 0.76f }, { ringsDamping, 0.88f }, { ringsPosition, 0.52f }, { ringsTune, 0.0f },
                { cloudsMode, 0.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.50f }, { cloudsSize, 0.82f }, { cloudsDensity, 0.78f }, { cloudsTexture, 0.75f },
                { cloudsPitch, 12.0f }, { cloudsFeedback, 0.48f }, { cloudsReverb, 0.78f }, { cloudsDryWet, 0.55f },
                { lfo1Rate, 0.18f }, { lfo1Shape, 0.0f }, { lfo2Rate, 0.12f }, { lfo2Shape, 1.0f }, { lfo3Rate, 0.08f }, { lfo3Shape, 0.0f },
                { env1Attack, 0.008f }, { env1Decay, 2.6f }, { env1Sustain, 0.35f }, { env1Release, 4.5f },
                { env2Attack, 0.005f }, { env2Decay, 1.4f }, { env2Sustain, 0.10f }, { env2Release, 2.0f },
                { env3Attack, 0.500f }, { env3Decay, 3.0f }, { env3Sustain, 0.70f }, { env3Release, 4.8f },
                { marblesEnable, 0.0f }, { marblesTempoSync, 1.0f }, { marblesDivision, 2.0f }, { marblesRate, 1.8f },
                { marblesDejaVu, 0.50f }, { marblesSpread, 0.55f }, { marblesSteps, 0.85f },
                { polyphonyMode, 2.0f }, { masterGain, 0.0f }
            },
            true, {}
        },
        {
            "III. Generative Deja-Vu Loom",
            "Generative Loom",
            "DAW-synced Marbles constellation plucking dispersion strings in a looping delay.",
            {
                { exciterBypass, 0.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 0.0f },
                { inputSource, 0.0f }, { oscEngine, 10.0f }, { oscTune, 0.0f },
                { oscHarmonics, 0.50f }, { oscTimbre, 0.62f }, { oscMorph, 0.44f },
                { subBlend, 0.40f }, { strikeLevel, 0.75f }, { directMix, 0.15f },
                { ringsModel, 2.0f }, { ringsPoly, 1.0f },
                { ringsStructure, 0.48f }, { ringsBrightness, 0.72f }, { ringsDamping, 0.76f }, { ringsPosition, 0.41f }, { ringsTune, 0.0f },
                { cloudsMode, 2.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.33f }, { cloudsSize, 0.58f }, { cloudsDensity, 0.32f }, { cloudsTexture, 0.54f },
                { cloudsPitch, 0.0f }, { cloudsFeedback, 0.52f }, { cloudsReverb, 0.58f }, { cloudsDryWet, 0.42f },
                { lfo1Rate, 0.42f }, { lfo1Shape, 0.0f }, { lfo2Rate, 0.31f }, { lfo2Shape, 3.0f }, { lfo3Rate, 0.15f }, { lfo3Shape, 1.0f },
                { env1Attack, 0.005f }, { env1Decay, 1.1f }, { env1Sustain, 0.25f }, { env1Release, 2.0f },
                { env2Attack, 0.005f }, { env2Decay, 0.8f }, { env2Sustain, 0.15f }, { env2Release, 1.2f },
                { env3Attack, 0.100f }, { env3Decay, 1.8f }, { env3Sustain, 0.50f }, { env3Release, 2.5f },
                { marblesEnable, 1.0f }, { marblesTempoSync, 1.0f }, { marblesDivision, 4.0f }, { marblesRate, 4.2f },
                { marblesDejaVu, 0.52f }, { marblesSpread, 0.62f }, { marblesSteps, 0.88f },
                { polyphonyMode, 2.0f }, { masterGain, 0.0f }
            },
            true, {}
        },
        {
            "IV. Bowed Cello in Subzero Fog",
            "Bowed & Ambient",
            "Continuous particle friction exciting warm wooden resonances and time-stretched clouds.",
            {
                { exciterBypass, 0.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 0.0f },
                { inputSource, 0.0f }, { oscEngine, 0.0f }, { oscTune, -12.0f },
                { oscHarmonics, 0.32f }, { oscTimbre, 0.48f }, { oscMorph, 0.65f },
                { subBlend, 0.55f }, { strikeLevel, 0.35f }, { directMix, 0.28f },
                { ringsModel, 5.0f }, { ringsPoly, 1.0f },
                { ringsStructure, 0.28f }, { ringsBrightness, 0.54f }, { ringsDamping, 0.86f }, { ringsPosition, 0.26f }, { ringsTune, 0.0f },
                { cloudsMode, 1.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.62f }, { cloudsSize, 0.78f }, { cloudsDensity, 0.72f }, { cloudsTexture, 0.82f },
                { cloudsPitch, 0.0f }, { cloudsFeedback, 0.42f }, { cloudsReverb, 0.72f }, { cloudsDryWet, 0.52f },
                { lfo1Rate, 0.15f }, { lfo1Shape, 0.0f }, { lfo2Rate, 0.09f }, { lfo2Shape, 0.0f }, { lfo3Rate, 0.06f }, { lfo3Shape, 1.0f },
                { env1Attack, 0.450f }, { env1Decay, 3.2f }, { env1Sustain, 0.82f }, { env1Release, 4.2f },
                { env2Attack, 0.650f }, { env2Decay, 2.8f }, { env2Sustain, 0.60f }, { env2Release, 3.8f },
                { env3Attack, 1.200f }, { env3Decay, 4.0f }, { env3Sustain, 0.75f }, { env3Release, 5.0f },
                { marblesEnable, 0.0f }, { marblesTempoSync, 0.0f }, { marblesDivision, 2.0f }, { marblesRate, 1.5f },
                { marblesDejaVu, 0.45f }, { marblesSpread, 0.30f }, { marblesSteps, 0.40f },
                { polyphonyMode, 2.0f }, { masterGain, 0.0f }
            },
            true, {}
        },
        {
            "V. West-Coast Buchla Wavefold",
            "Plucked & Sympathetic",
            "Harmonic wavefolding cascading into quantized sympathetic chords.",
            {
                { exciterBypass, 0.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 0.0f },
                { inputSource, 0.0f }, { oscEngine, 1.0f }, { oscTune, 0.0f },
                { oscHarmonics, 0.58f }, { oscTimbre, 0.72f }, { oscMorph, 0.48f },
                { subBlend, 0.45f }, { strikeLevel, 0.55f }, { directMix, 0.34f },
                { ringsModel, 4.0f }, { ringsPoly, 2.0f },
                { ringsStructure, 0.50f }, { ringsBrightness, 0.70f }, { ringsDamping, 0.78f }, { ringsPosition, 0.46f }, { ringsTune, 0.0f },
                { cloudsMode, 0.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.44f }, { cloudsSize, 0.64f }, { cloudsDensity, 0.68f }, { cloudsTexture, 0.62f },
                { cloudsPitch, 7.0f }, { cloudsFeedback, 0.38f }, { cloudsReverb, 0.60f }, { cloudsDryWet, 0.38f },
                { lfo1Rate, 0.36f }, { lfo1Shape, 1.0f }, { lfo2Rate, 0.22f }, { lfo2Shape, 0.0f }, { lfo3Rate, 0.14f }, { lfo3Shape, 2.0f },
                { env1Attack, 0.004f }, { env1Decay, 1.4f }, { env1Sustain, 0.30f }, { env1Release, 2.5f },
                { env2Attack, 0.003f }, { env2Decay, 0.9f }, { env2Sustain, 0.15f }, { env2Release, 1.5f },
                { env3Attack, 0.200f }, { env3Decay, 2.0f }, { env3Sustain, 0.50f }, { env3Release, 3.0f },
                { marblesEnable, 0.0f }, { marblesTempoSync, 1.0f }, { marblesDivision, 4.0f }, { marblesRate, 3.0f },
                { marblesDejaVu, 0.50f }, { marblesSpread, 0.50f }, { marblesSteps, 0.70f },
                { polyphonyMode, 3.0f }, { masterGain, 0.0f }
            },
            true, {}
        },
        {
            "VI. Spectral Memory Archive",
            "Bowed & Ambient",
            "Formant vowel swarms suspended inside spectral phase-warped granular amber.",
            {
                { exciterBypass, 0.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 0.0f },
                { inputSource, 0.0f }, { oscEngine, 7.0f }, { oscTune, 0.0f },
                { oscHarmonics, 0.48f }, { oscTimbre, 0.64f }, { oscMorph, 0.58f },
                { subBlend, 0.50f }, { strikeLevel, 0.45f }, { directMix, 0.35f },
                { ringsModel, 0.0f }, { ringsPoly, 1.0f },
                { ringsStructure, 0.42f }, { ringsBrightness, 0.62f }, { ringsDamping, 0.84f }, { ringsPosition, 0.58f }, { ringsTune, 0.0f },
                { cloudsMode, 3.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.55f }, { cloudsSize, 0.75f }, { cloudsDensity, 0.76f }, { cloudsTexture, 0.80f },
                { cloudsPitch, 0.0f }, { cloudsFeedback, 0.55f }, { cloudsReverb, 0.82f }, { cloudsDryWet, 0.62f },
                { lfo1Rate, 0.22f }, { lfo1Shape, 0.0f }, { lfo2Rate, 0.14f }, { lfo2Shape, 0.0f }, { lfo3Rate, 0.09f }, { lfo3Shape, 1.0f },
                { env1Attack, 0.250f }, { env1Decay, 2.8f }, { env1Sustain, 0.70f }, { env1Release, 5.0f },
                { env2Attack, 0.400f }, { env2Decay, 3.0f }, { env2Sustain, 0.50f }, { env2Release, 4.0f },
                { env3Attack, 0.800f }, { env3Decay, 3.5f }, { env3Sustain, 0.65f }, { env3Release, 4.5f },
                { marblesEnable, 0.0f }, { marblesTempoSync, 0.0f }, { marblesDivision, 2.0f }, { marblesRate, 2.0f },
                { marblesDejaVu, 0.48f }, { marblesSpread, 0.68f }, { marblesSteps, 0.30f },
                { polyphonyMode, 2.0f }, { masterGain, 0.0f }
            },
            true, {}
        },
        {
            "VII. External Audio -> Rings & Clouds FX",
            "External FX Processor",
            "Live external audio input exciting sympathetic strings and granular Oliverb mist (Play MIDI to retune).",
            {
                { exciterBypass, 1.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 0.0f },
                { inputSource, 1.0f }, { oscEngine, 9.0f }, { oscTune, 0.0f },
                { oscHarmonics, 0.50f }, { oscTimbre, 0.50f }, { oscMorph, 0.50f },
                { subBlend, 0.0f }, { strikeLevel, 0.25f }, { directMix, 0.20f },
                { ringsModel, 0.0f }, { ringsPoly, 2.0f },
                { ringsStructure, 0.38f }, { ringsBrightness, 0.74f }, { ringsDamping, 0.85f }, { ringsPosition, 0.36f }, { ringsTune, 0.0f },
                { cloudsMode, 0.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.40f }, { cloudsSize, 0.68f }, { cloudsDensity, 0.74f }, { cloudsTexture, 0.70f },
                { cloudsPitch, 0.0f }, { cloudsFeedback, 0.40f }, { cloudsReverb, 0.68f }, { cloudsDryWet, 0.50f },
                { lfo1Rate, 0.25f }, { lfo1Shape, 0.0f }, { lfo2Rate, 0.16f }, { lfo2Shape, 1.0f }, { lfo3Rate, 0.10f }, { lfo3Shape, 0.0f },
                { env1Attack, 0.010f }, { env1Decay, 2.0f }, { env1Sustain, 0.80f }, { env1Release, 3.5f },
                { env2Attack, 0.020f }, { env2Decay, 1.5f }, { env2Sustain, 0.50f }, { env2Release, 2.5f },
                { env3Attack, 0.200f }, { env3Decay, 2.5f }, { env3Sustain, 0.60f }, { env3Release, 3.5f },
                { marblesEnable, 0.0f }, { marblesTempoSync, 1.0f }, { marblesDivision, 4.0f }, { marblesRate, 2.5f },
                { marblesDejaVu, 0.50f }, { marblesSpread, 0.40f }, { marblesSteps, 0.75f },
                { polyphonyMode, 2.0f }, { masterGain, 0.0f }
            },
            true, {}
        },
        {
            "VIII. Standalone Rings Modal Marimba",
            "Modal Bells",
            "Pure Rings physical modeling resonator with Clouds bypassed for immediate acoustic transients.",
            {
                { exciterBypass, 1.0f }, { ringsBypass, 0.0f }, { cloudsBypass, 1.0f },
                { inputSource, 0.0f }, { oscEngine, 11.0f }, { oscTune, 0.0f },
                { oscHarmonics, 0.35f }, { oscTimbre, 0.48f }, { oscMorph, 0.25f },
                { subBlend, 0.15f }, { strikeLevel, 0.88f }, { directMix, 0.0f },
                { ringsModel, 0.0f }, { ringsPoly, 1.0f },
                { ringsStructure, 0.24f }, { ringsBrightness, 0.64f }, { ringsDamping, 0.74f }, { ringsPosition, 0.28f }, { ringsTune, 0.0f },
                { cloudsMode, 0.0f }, { cloudsFreeze, 0.0f },
                { cloudsPosition, 0.35f }, { cloudsSize, 0.60f }, { cloudsDensity, 0.70f }, { cloudsTexture, 0.65f },
                { cloudsPitch, 0.0f }, { cloudsFeedback, 0.25f }, { cloudsReverb, 0.45f }, { cloudsDryWet, 0.35f },
                { lfo1Rate, 0.20f }, { lfo1Shape, 0.0f }, { lfo2Rate, 0.12f }, { lfo2Shape, 0.0f }, { lfo3Rate, 0.08f }, { lfo3Shape, 0.0f },
                { env1Attack, 0.002f }, { env1Decay, 1.2f }, { env1Sustain, 0.20f }, { env1Release, 2.2f },
                { env2Attack, 0.002f }, { env2Decay, 0.8f }, { env2Sustain, 0.10f }, { env2Release, 1.5f },
                { env3Attack, 0.050f }, { env3Decay, 1.5f }, { env3Sustain, 0.40f }, { env3Release, 2.0f },
                { marblesEnable, 0.0f }, { marblesTempoSync, 1.0f }, { marblesDivision, 4.0f }, { marblesRate, 3.0f },
                { marblesDejaVu, 0.50f }, { marblesSpread, 0.45f }, { marblesSteps, 0.80f },
                { polyphonyMode, 3.0f }, { masterGain, 0.0f }
            },
            true, {}
        }
    };
}

} // namespace ric::presets
