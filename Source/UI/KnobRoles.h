#pragma once
#include <array>

namespace ric::ui {

// What a dial does depends on the selected voice model, resonator model or cloud mode.
// These tables describe what the DSP in this plugin actually does with each dial.
struct KnobRole {
    const char* label;
    const char* tooltip;
    bool used = true;
};

namespace Roles {

inline constexpr KnobRole kUnused { "Unused", "Not used by this mode", false };

// I. the breath: Harmonics, Timbre, Morph for each of the 12 voice models
inline constexpr std::array<std::array<KnobRole, 3>, 12> kVoice {{
    {{ { "Detune", "Detunes the second oscillator, up to a fifth either way" },
       { "Saw / Pulse", "Crossfades from sawtooth to pulse" },
       { "Pulse Width", "Width of the pulse wave" } }},
    {{ { "Sine / Tri", "Blends the folded wave from sine to triangle" },
       { "Fold", "How hard the wave is driven into the folder" },
       { "Symmetry", "Bends the folding to one side, adding even harmonics" } }},
    {{ { "Ratio", "Frequency ratio of the modulator" },
       { "Index", "FM depth: brightness, which also grows with each strike" },
       { "Feedback", "Modulator feedback: clean in the centre, rougher either side" } }},
    {{ { "Formant 1", "The lower vowel resonance" },
       { "Formant 2", "The upper vowel resonance" },
       { "Narrowness", "Shortens each pulse: from round to buzzy" } }},
    {{ { "Even / Odd", "Balance of even against odd partials" },
       { "Centre", "Which partial the spectrum gathers around" },
       { "Focus", "Narrows the spectrum around its centre" } }},
    {{ { "Row", "Moves between the two rows of waves" },
       { "Column", "Moves along each row of waves" },
       { "Warp", "Phase distortion of the wave" } }},
    {{ { "Chord", "Octave, fifth, minor 7, major 7, minor 9, major 9, sus4, phrygian" },
       { "Inversion", "Above the middle, lifts the root an octave" },
       { "Waveform", "From sine organ to saw strings" } }},
    {{ { "Spread", "How far the voices of the swarm drift apart" },
       { "Formant", "The resonance inside each voice of the swarm" },
       { "Noise", "Breath noise mixed into the swarm" } }},
    {{ { "Clocked", "From white noise to stepped, clocked noise" },
       { "Clock", "Clock rate and filter frequency" },
       { "Resonance", "Resonance of the filter" } }},
    {{ { "Scatter", "How far each particle's pitch scatters" },
       { "Particles", "Density of the friction particles" },
       { "Bow", "Steady bow noise and filter resonance" } }},
    {{ { "Burst", "How much noise plucks the string" },
       { "Brightness", "The string's damping filter: dull to bright" },
       { "Sustain", "How long the string rings" } }},
    {{ { "Overtone", "Ratio of the second partial of the strike" },
       { "Click", "Loudness of the strike's click" },
       { "Body", "How long the struck body sustains" } }},
}};

// II. the body: Structure, Brightness, Damping, Position for each of the 6 resonator models
inline constexpr std::array<std::array<KnobRole, 4>, 6> kBody {{
    {{ { "Inharmonic", "From pure harmonic partials to bell, bar and plate partials" },
       { "Brightness", "How long the high partials last" },
       { "Decay", "How long the body goes on ringing" },
       { "Strike Point", "Where the body is struck: changes which partials sound" } }},
    {{ { "Intervals", "Tuning of the sympathetic strings: octaves, fifths, chords" },
       { "Brightness", "Dampens or opens the strings' high end" },
       { "Decay", "How long the strings go on ringing" },
       { "Pickup", "Where the strings are heard: thin near the end, full in the middle" } }},
    {{ { "Dispersion", "Stiffness and detuning of the strings; past the middle, pitch bends with energy" },
       { "Brightness", "Dampens or opens the string's high end" },
       { "Decay", "How long the string goes on ringing" },
       { "Pickup", "Where the string is heard: thin near the end, full in the middle" } }},
    {{ { "Ratio", "Ratio of the FM voice's modulator" },
       { "Index", "FM depth: how bright the voice becomes" },
       { "Decay", "How long the voice lingers after its exciter" },
       { "Feedback", "Modulator feedback: clean in the centre, rougher either side" } }},
    {{ { "Chord", "Which chord the four strings form" },
       { "Brightness", "Dampens or opens the strings' high end" },
       { "Decay", "How long the strings go on ringing" },
       { "Pickup", "Where the strings are heard" } }},
    {{ { "Dispersion", "Stiffness and detuning of the strings" },
       { "Brightness", "The strings' high end, and the cavern's tone" },
       { "Decay", "How long the strings ring, and the size of the cavern" },
       { "Space", "Where the strings are heard, and how much cavern you hear" } }},
}};

// III. the sky: Position, Size, Density, Texture, Pitch, Feedback for each of the 4 cloud modes
inline constexpr std::array<std::array<KnobRole, 6>, 4> kSky {{
    {{ { "Position", "How far back in time the grains listen (or drag across the sky above)" },
       { "Size", "Length of each grain, 15 ms to 650 ms" },
       { "Density", "Sparse in the centre; regular grains to the left, scattered to the right" },
       { "Texture", "Softness of each grain's edge, and the reverb's diffusion" },
       { "Pitch", "Transposes the grains, in semitones" },
       { "Feedback", "Feeds the cloud back into its own memory" } }},
    {{ { "Delay", "How far behind the shifted voice follows, 10 ms to 1.2 s" },
       { "Window", "Length of the crossfading windows: short is grainy, long is smooth" },
       kUnused,
       { "Diffusion", "Softens the feedback and the reverb's diffusion" },
       { "Pitch", "Transposition without changing time, in semitones" },
       { "Feedback", "Feeds the shifted sound back: rising or falling spirals" } }},
    {{ { "Delay", "How long ago the loop ends, 10 ms to 1.5 s" },
       { "Loop", "Length of the looped segment, 40 ms to 1.8 s" },
       kUnused,
       { "Smoothness", "Edges of each pass: hard cuts to soft swells" },
       { "Speed", "Playback speed of the loop, like tape, in semitones" },
       { "Repeats", "Feedback, crossing between left and right" } }},
    {{ { "Position", "How far back the grains listen, and the shift of the resonant bands" },
       { "Size", "Grain length, and the balance of odd and even bands" },
       { "Density", "Sparse in the centre; regular grains to the left, scattered to the right" },
       { "Resonance", "How sharply the spectral bands ring" },
       { "Pitch", "Transposes the grains, in semitones" },
       { "Feedback", "Feeds the cloud back into its own memory" } }},
}};

} // namespace Roles
} // namespace ric::ui
