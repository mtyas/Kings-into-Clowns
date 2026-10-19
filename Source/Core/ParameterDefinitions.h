#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace ric::params {

namespace ID {
    // Stage Bypass Switches
    inline constexpr const char* exciterBypass    = "exciter_bypass";
    inline constexpr const char* ringsBypass      = "rings_bypass";
    inline constexpr const char* cloudsBypass     = "clouds_bypass";

    // I. Voice & Exciter Stage
    inline constexpr const char* inputSource      = "input_source";      // 0=Internal Osc, 1=External Audio FX, 2=Osc + External Audio
    inline constexpr const char* oscEngine        = "osc_engine";        // 0..11 Macro Engines
    inline constexpr const char* oscTune          = "osc_tune";          // -24..+24 semitones
    inline constexpr const char* oscHarmonics     = "osc_harmonics";     // 0..1
    inline constexpr const char* oscTimbre        = "osc_timbre";        // 0..1
    inline constexpr const char* oscMorph         = "osc_morph";         // 0..1
    inline constexpr const char* subBlend         = "sub_blend";         // 0..1 (Braids Sub/Harmonic Layer)
    inline constexpr const char* strikeLevel      = "strike_level";      // 0..1 (Mallet / Transient Strike Level)
    inline constexpr const char* directMix        = "direct_mix";        // 0..1 (Raw Exciter blend bypassing Rings)

    // II. Resonator Body (Rings)
    inline constexpr const char* ringsModel       = "rings_model";       // 0..5
    inline constexpr const char* ringsPoly        = "rings_poly";        // 0=1 String, 1=2 Strings, 2=4 Strings
    inline constexpr const char* ringsStructure   = "rings_structure";   // 0..1
    inline constexpr const char* ringsBrightness  = "rings_brightness";  // 0..1
    inline constexpr const char* ringsDamping     = "rings_damping";     // 0..1
    inline constexpr const char* ringsPosition    = "rings_position";    // 0..1
    inline constexpr const char* ringsTune        = "rings_tune";        // -24..+24 semitones offset from MIDI note

    // III. Cloud & Space (Clouds + Oliverb)
    inline constexpr const char* cloudsMode       = "clouds_mode";       // 0..3
    inline constexpr const char* cloudsFreeze     = "clouds_freeze";     // 0 or 1
    inline constexpr const char* cloudsPosition   = "clouds_position";   // 0..1
    inline constexpr const char* cloudsSize       = "clouds_size";       // 0..1
    inline constexpr const char* cloudsDensity    = "clouds_density";    // 0..1
    inline constexpr const char* cloudsTexture    = "clouds_texture";    // 0..1
    inline constexpr const char* cloudsPitch      = "clouds_pitch";      // -24..+24 semitones
    inline constexpr const char* cloudsFeedback   = "clouds_feedback";   // 0..1
    inline constexpr const char* cloudsReverb     = "clouds_reverb";     // 0..1
    inline constexpr const char* cloudsDryWet     = "clouds_dry_wet";    // 0..1
    inline constexpr const char* cloudsSpread     = "clouds_spread";     // 0..1 stereo width of the cloud

    // IV. 3 Independent LFOs
    inline constexpr const char* lfo1Rate         = "lfo1_rate";         // 0.02..20 Hz
    inline constexpr const char* lfo1Shape        = "lfo1_shape";        // 0..3 (Sine, Tri, Saw, S&H)
    inline constexpr const char* lfo2Rate         = "lfo2_rate";         // 0.02..20 Hz
    inline constexpr const char* lfo2Shape        = "lfo2_shape";        // 0..3
    inline constexpr const char* lfo3Rate         = "lfo3_rate";         // 0.02..20 Hz
    inline constexpr const char* lfo3Shape        = "lfo3_shape";        // 0..3
    inline constexpr const char* lfo1Sync         = "lfo1_sync";         // follow host tempo
    inline constexpr const char* lfo1Division     = "lfo1_division";
    inline constexpr const char* lfo2Sync         = "lfo2_sync";
    inline constexpr const char* lfo2Division     = "lfo2_division";
    inline constexpr const char* lfo3Sync         = "lfo3_sync";
    inline constexpr const char* lfo3Division     = "lfo3_division";

    // V. 3 Independent ADSR Envelopes (ENV 1 = Amp + Mod, ENV 2 = Mod, ENV 3 = Mod)
    inline constexpr const char* env1Attack       = "env1_attack";
    inline constexpr const char* env1Decay        = "env1_decay";
    inline constexpr const char* env1Sustain      = "env1_sustain";
    inline constexpr const char* env1Release      = "env1_release";

    inline constexpr const char* env2Attack       = "env2_attack";
    inline constexpr const char* env2Decay        = "env2_decay";
    inline constexpr const char* env2Sustain      = "env2_sustain";
    inline constexpr const char* env2Release      = "env2_release";

    inline constexpr const char* env3Attack       = "env3_attack";
    inline constexpr const char* env3Decay        = "env3_decay";
    inline constexpr const char* env3Sustain      = "env3_sustain";
    inline constexpr const char* env3Release      = "env3_release";

    // VI. Generative Loom (Marbles)
    inline constexpr const char* marblesEnable    = "marbles_enable";
    inline constexpr const char* marblesTempoSync = "marbles_sync";
    inline constexpr const char* marblesDivision  = "marbles_division";
    inline constexpr const char* marblesRate      = "marbles_rate";
    inline constexpr const char* marblesDejaVu    = "marbles_deja_vu";
    inline constexpr const char* marblesSpread    = "marbles_spread";
    inline constexpr const char* marblesSteps     = "marbles_steps";
    inline constexpr const char* marblesLength    = "marbles_length";    // 1..16 loop steps
    inline constexpr const char* marblesJitter    = "marbles_jitter";    // 0..1 clock drift
    inline constexpr const char* marblesPitchTarget = "marbles_pitch_target"; // where the loom's melody goes

    // Global Voice & Master
    inline constexpr const char* polyphonyMode    = "polyphony_mode";    // 0=Mono, 1=4v, 2=8v, 3=16v
    inline constexpr const char* masterGain       = "master_gain";       // -24..+6 dB
}

inline const juce::StringArray kInputSourceNames {
    "Internal Synth Voice",
    "External Audio Input (FX)",
    "Synth + External Audio"
};

inline const juce::StringArray kOscEngineNames {
    "Virtual Analog (CS-80 Saw/Pulse)",
    "Buchla Wavefolder",
    "Two-Operator FM",
    "Vowel & Formant",
    "24-Harmonic Additive",
    "Wavetable Matrix",
    "Solina String Ensemble",
    "Formant Swarm Cloud",
    "Filtered Clocked Noise",
    "Bowed Particle Friction",
    "Karplus-Strong Pluck",
    "Modal Mallet Strike"
};

inline const juce::StringArray kRingsModelNames {
    "Modal Resonator (Bell / Plate)",
    "Sympathetic Strings (Koto / Sitar)",
    "Inharmonic Dispersion String",
    "FM Follower Resonator",
    "Sympathetic Quantized Chords",
    "String & Cavern Reverb"
};

inline const juce::StringArray kRingsPolyNames {
    "1 String / Voice",
    "2 Strings / Voice",
    "4 Strings / Voice"
};

inline const juce::StringArray kCloudsModeNames {
    "Granular Texture (Clouds)",
    "Pitch Shifter / Time Stretch",
    "Looping Tap Delay",
    "Spectral Phase Warp"
};

inline const juce::StringArray kLfoShapeNames {
    "Sine",
    "Triangle",
    "Sawtooth",
    "Sample & Hold"
};

// One LFO cycle per division; kLfoDivisionBeats holds its length in quarter notes
inline const juce::StringArray kLfoDivisionNames {
    "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2 Dotted", "1/4", "1/4 Dotted", "1/4 Triplet",
    "1/8", "1/8 Dotted", "1/8 Triplet", "1/16", "1/16 Triplet", "1/32"
};
inline constexpr std::array<float, 14> kLfoDivisionBeats {
    16.0f, 8.0f, 4.0f, 2.0f, 3.0f, 1.0f, 1.5f, 2.0f / 3.0f,
    0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f
};

inline const juce::StringArray kLoomPitchTargetNames {
    "Rhythm only",
    "Voice & Body",
    "Voice only",
    "Body only"
};

inline const juce::StringArray kMarblesDivisionNames {
    "1/1 Bar",
    "1/2 Note",
    "1/4 Note",
    "1/8 Dotted",
    "1/8 Note",
    "1/8 Triplet",
    "1/16 Dotted",
    "1/16 Note",
    "1/16 Triplet",
    "1/32 Note"
};

inline const juce::StringArray kPolyphonyModeNames {
    "Mono / Legato",
    "4 Voices",
    "8 Voices",
    "16 Voices"
};

} // namespace ric::params
