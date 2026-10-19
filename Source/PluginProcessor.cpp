#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

KingsIntoClownsProcessor::KingsIntoClownsProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Audio Input (FX Exciter)", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, &undoManager_, "KingsIntoClownsState", createParameterLayout()) {
    bindClapPolyCallbacks();
    cacheRawParameters();
    midiLearn_.bindParameterList(getParameters());
    presetManager_ = std::make_unique<ric::core::PresetManager>(apvts_, synthEngine_.getModMatrix(), undoManager_);
    presetManager_->loadPreset(0);
    undoManager_.clearUndoHistory();
}

juce::AudioProcessorValueTreeState::ParameterLayout KingsIntoClownsProcessor::createParameterLayout() {
    using namespace ric::params;
    using namespace ric::mod;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;

    auto addPolyFloat = [&](const char* id, const char* name, float minV, float maxV, float stepV, float defV, Dest destTag, bool isPoly = true) {
        juce::NormalisableRange<float> range(minV, maxV, stepV);
        auto param = std::make_unique<ric::core::ClapPolyParameter>(
            juce::ParameterID{ id, 1 }, name, range, defV, static_cast<int>(destTag), isPoly);
        polyParams_.push_back(param.get());
        layout.push_back(std::move(param));
    };

    // 0. Stage Bypass Switches
    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ID::exciterBypass, 1 }, "Bypass Exciter", false));
    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ID::ringsBypass, 1 }, "Bypass Rings Resonator", false));
    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ID::cloudsBypass, 1 }, "Bypass Clouds Texture", false));

    // 1. Voice & Exciter Stage
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::inputSource, 1 }, "Input Source", kInputSourceNames, 0));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::oscEngine, 1 }, "Voice Model", kOscEngineNames, 9));
    addPolyFloat(ID::oscTune,       "Voice Tune",        -24.0f, 24.0f, 0.01f,  0.0f,  Dest::OscTune,       true);
    addPolyFloat(ID::oscHarmonics,  "Harmonics",           0.0f,  1.0f, 0.001f, 0.42f, Dest::OscHarmonics,  true);
    addPolyFloat(ID::oscTimbre,     "Timbre",              0.0f,  1.0f, 0.001f, 0.58f, Dest::OscTimbre,     true);
    addPolyFloat(ID::oscMorph,      "Morph",               0.0f,  1.0f, 0.001f, 0.31f, Dest::OscMorph,      true);
    addPolyFloat(ID::subBlend,      "Sub Layer",           0.0f,  1.0f, 0.001f, 0.25f, Dest::SubBlend,      true);
    addPolyFloat(ID::strikeLevel,   "Mallet Strike",       0.0f,  1.0f, 0.001f, 0.65f, Dest::StrikeLevel,   true);
    addPolyFloat(ID::directMix,     "Direct Mix",          0.0f,  1.0f, 0.001f, 0.12f, Dest::DirectMix,     true);

    // 2. Resonator Body (Rings)
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::ringsModel, 1 }, "Resonator Model", kRingsModelNames, 1));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::ringsPoly, 1 }, "Resonator Strings", kRingsPolyNames, 2));
    addPolyFloat(ID::ringsStructure,  "Rings Structure",   0.0f,  1.0f, 0.001f, 0.36f, Dest::RingsStructure,  true);
    addPolyFloat(ID::ringsBrightness, "Rings Brightness",  0.0f,  1.0f, 0.001f, 0.68f, Dest::RingsBrightness, true);
    addPolyFloat(ID::ringsDamping,    "Rings Damping",     0.0f,  1.0f, 0.001f, 0.82f, Dest::RingsDamping,    true);
    addPolyFloat(ID::ringsPosition,   "Rings Position",    0.0f,  1.0f, 0.001f, 0.34f, Dest::RingsPosition,   true);
    addPolyFloat(ID::ringsTune,       "Rings Tune",      -24.0f, 24.0f, 0.01f,  0.0f,  Dest::RingsTune,       true);

    // 3. Cloud & Space (Clouds + Oliverb)
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::cloudsMode, 1 }, "Cloud Mode", kCloudsModeNames, 0));
    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ID::cloudsFreeze, 1 }, "Cloud Freeze", false));
    addPolyFloat(ID::cloudsPosition,  "Cloud Position",    0.0f,  1.0f, 0.001f, 0.38f, Dest::CloudsPosition,  false);
    addPolyFloat(ID::cloudsSize,      "Grain Size",        0.0f,  1.0f, 0.001f, 0.66f, Dest::CloudsSize,      false);
    addPolyFloat(ID::cloudsDensity,   "Grain Density",     0.0f,  1.0f, 0.001f, 0.74f, Dest::CloudsDensity,   false);
    addPolyFloat(ID::cloudsTexture,   "Grain Texture",     0.0f,  1.0f, 0.001f, 0.68f, Dest::CloudsTexture,   false);
    addPolyFloat(ID::cloudsPitch,     "Cloud Pitch",     -24.0f, 24.0f, 0.01f,  0.0f,  Dest::CloudsPitch,     false);
    addPolyFloat(ID::cloudsFeedback,  "Cloud Feedback",    0.0f,  1.0f, 0.001f, 0.35f, Dest::CloudsFeedback,  false);
    addPolyFloat(ID::cloudsReverb,    "Space Reverb",      0.0f,  1.0f, 0.001f, 0.64f, Dest::CloudsReverb,    false);
    addPolyFloat(ID::cloudsDryWet,    "Cloud Mix",         0.0f,  1.0f, 0.001f, 0.46f, Dest::CloudsDryWet,    false);
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::cloudsSpread, 1 }, "Cloud Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.82f));

    // 4. 3 Independent LFOs
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::lfo1Rate, 1 }, "LFO 1 Rate",
        juce::NormalisableRange<float>(0.02f, 20.0f, 0.001f, 0.35f), 0.28f));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::lfo1Shape, 1 }, "LFO 1 Shape", kLfoShapeNames, 0));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::lfo2Rate, 1 }, "LFO 2 Rate",
        juce::NormalisableRange<float>(0.02f, 20.0f, 0.001f, 0.35f), 0.19f));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::lfo2Shape, 1 }, "LFO 2 Shape", kLfoShapeNames, 1));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::lfo3Rate, 1 }, "LFO 3 Rate",
        juce::NormalisableRange<float>(0.02f, 20.0f, 0.001f, 0.35f), 0.11f));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::lfo3Shape, 1 }, "LFO 3 Shape", kLfoShapeNames, 0));

    const char* syncIds[3] = { ID::lfo1Sync, ID::lfo2Sync, ID::lfo3Sync };
    const char* divIds[3]  = { ID::lfo1Division, ID::lfo2Division, ID::lfo3Division };
    for (int i = 0; i < 3; ++i) {
        const juce::String n = "LFO " + juce::String(i + 1);
        layout.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ syncIds[i], 1 }, n + " Tempo Sync", false));
        layout.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ divIds[i], 1 }, n + " Division",
                                                                      kLfoDivisionNames, 2 + i));
    }

    // 5. 3 Independent ADSR Envelopes
    auto addAdsr = [&](const char* idA, const char* idD, const char* idS, const char* idR, const juce::String& prefix) {
        layout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ idA, 1 }, prefix + " Attack",
            juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.35f), 0.015f));
        layout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ idD, 1 }, prefix + " Decay",
            juce::NormalisableRange<float>(0.01f, 8.0f, 0.001f, 0.4f), 1.8f));
        layout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ idS, 1 }, prefix + " Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.40f));
        layout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ idR, 1 }, prefix + " Release",
            juce::NormalisableRange<float>(0.02f, 12.0f, 0.001f, 0.4f), 3.2f));
    };
    addAdsr(ID::env1Attack, ID::env1Decay, ID::env1Sustain, ID::env1Release, "ENV 1 (Amp)");
    addAdsr(ID::env2Attack, ID::env2Decay, ID::env2Sustain, ID::env2Release, "ENV 2");
    addAdsr(ID::env3Attack, ID::env3Decay, ID::env3Sustain, ID::env3Release, "ENV 3");

    // 6. Generative Loom (Marbles)
    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ID::marblesEnable, 1 }, "Loom Enable", false));
    layout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ID::marblesTempoSync, 1 }, "Loom DAW Sync", true));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::marblesDivision, 1 }, "Loom Sync Division", kMarblesDivisionNames, 4));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::marblesRate, 1 }, "Loom Free Rate",
        juce::NormalisableRange<float>(0.25f, 16.0f, 0.01f, 0.45f), 2.5f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::marblesDejaVu, 1 }, "Loom Deja Vu",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.50f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::marblesSpread, 1 }, "Loom Spread",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.45f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::marblesSteps, 1 }, "Loom Steps",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));
    layout.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ ID::marblesLength, 1 }, "Loom Length", 1, 16, 8));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::marblesJitter, 1 }, "Loom Drift",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.14f));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::marblesPitchTarget, 1 }, "Loom Melody To", kLoomPitchTargetNames, 1));

    // Global Voice & Master
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ID::polyphonyMode, 1 }, "Polyphony", kPolyphonyModeNames, 2));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ID::masterGain, 1 }, "Master Gain",
        juce::NormalisableRange<float>(-24.0f, 6.0f, 0.01f), 0.0f));

    return { layout.begin(), layout.end() };
}

void KingsIntoClownsProcessor::bindClapPolyCallbacks() {
    for (auto* p : polyParams_) {
        p->setPolyModCallback([this](int paramTag, int32_t noteId, int16_t channel, int16_t key, float amount) {
            synthEngine_.handleClapPolyModulation(static_cast<ric::mod::Dest>(paramTag), noteId, channel, key, amount);
        });
    }
}

void KingsIntoClownsProcessor::cacheRawParameters() {
    static constexpr const char* kIds[] = {
#define RIC_RAW_ID(name) ric::params::ID::name,
        RIC_RAW_PARAMS(RIC_RAW_ID)
#undef RIC_RAW_ID
    };
    static_assert(std::size(kIds) == static_cast<size_t>(Raw::Count));
    for (size_t i = 0; i < rawParams_.size(); ++i) {
        rawParams_[i] = apvts_.getRawParameterValue(kIds[i]);
        jassert(rawParams_[i] != nullptr);
    }
}

void KingsIntoClownsProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    synthEngine_.prepare(sampleRate, samplesPerBlock);
}

void KingsIntoClownsProcessor::releaseResources() {
    synthEngine_.reset();
}

bool KingsIntoClownsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto outSet = layouts.getMainOutputChannelSet();
    if (outSet != juce::AudioChannelSet::stereo())
        return false;

    const auto inSet = layouts.getMainInputChannelSet();
    return inSet.isDisabled()
        || inSet == juce::AudioChannelSet::mono()
        || inSet == juce::AudioChannelSet::stereo();
}

ric::dsp::EngineParameters KingsIntoClownsProcessor::gatherEngineParameters() const noexcept {
    using namespace ric::mod;
    ric::dsp::EngineParameters ep{};
    auto& vc = ep.voiceCtx;

    vc.exciterBypass = raw(Raw::exciterBypass) > 0.5f;
    vc.ringsBypass   = raw(Raw::ringsBypass) > 0.5f;
    vc.cloudsBypass  = raw(Raw::cloudsBypass) > 0.5f;

    vc.inputSource   = static_cast<int>(raw(Raw::inputSource));
    vc.oscEngine     = static_cast<int>(raw(Raw::oscEngine));
    vc.ringsModel    = static_cast<int>(raw(Raw::ringsModel));
    vc.ringsPoly     = static_cast<int>(raw(Raw::ringsPoly));

    vc.lfoRates  = { raw(Raw::lfo1Rate), raw(Raw::lfo2Rate), raw(Raw::lfo3Rate) };
    vc.lfoShapes = { static_cast<int>(raw(Raw::lfo1Shape)), static_cast<int>(raw(Raw::lfo2Shape)), static_cast<int>(raw(Raw::lfo3Shape)) };
    vc.lfoSync   = { raw(Raw::lfo1Sync) > 0.5f, raw(Raw::lfo2Sync) > 0.5f, raw(Raw::lfo3Sync) > 0.5f };
    auto beatsFor = [](float choice) {
        const auto idx = static_cast<size_t>(std::clamp(static_cast<int>(choice), 0, static_cast<int>(ric::params::kLfoDivisionBeats.size()) - 1));
        return ric::params::kLfoDivisionBeats[idx];
    };
    vc.lfoBeats  = { beatsFor(raw(Raw::lfo1Division)), beatsFor(raw(Raw::lfo2Division)), beatsFor(raw(Raw::lfo3Division)) };
    vc.loomPitchTarget = static_cast<int>(raw(Raw::marblesPitchTarget));

    vc.envs[0] = { raw(Raw::env1Attack), raw(Raw::env1Decay), raw(Raw::env1Sustain), raw(Raw::env1Release) };
    vc.envs[1] = { raw(Raw::env2Attack), raw(Raw::env2Decay), raw(Raw::env2Sustain), raw(Raw::env2Release) };
    vc.envs[2] = { raw(Raw::env3Attack), raw(Raw::env3Decay), raw(Raw::env3Sustain), raw(Raw::env3Release) };

    auto& base = vc.baseDestParams;
    auto setBase = [&base](Dest d, float v) { base[static_cast<size_t>(d)] = v; };
    setBase(Dest::OscTune,         raw(Raw::oscTune));
    setBase(Dest::OscHarmonics,    raw(Raw::oscHarmonics));
    setBase(Dest::OscTimbre,       raw(Raw::oscTimbre));
    setBase(Dest::OscMorph,        raw(Raw::oscMorph));
    setBase(Dest::SubBlend,        raw(Raw::subBlend));
    setBase(Dest::StrikeLevel,     raw(Raw::strikeLevel));
    setBase(Dest::DirectMix,       raw(Raw::directMix));
    setBase(Dest::RingsStructure,  raw(Raw::ringsStructure));
    setBase(Dest::RingsBrightness, raw(Raw::ringsBrightness));
    setBase(Dest::RingsDamping,    raw(Raw::ringsDamping));
    setBase(Dest::RingsPosition,   raw(Raw::ringsPosition));
    setBase(Dest::RingsTune,       raw(Raw::ringsTune));
    setBase(Dest::CloudsPosition,  raw(Raw::cloudsPosition));
    setBase(Dest::CloudsSize,      raw(Raw::cloudsSize));
    setBase(Dest::CloudsDensity,   raw(Raw::cloudsDensity));
    setBase(Dest::CloudsTexture,   raw(Raw::cloudsTexture));
    setBase(Dest::CloudsPitch,     raw(Raw::cloudsPitch));
    setBase(Dest::CloudsFeedback,  raw(Raw::cloudsFeedback));
    setBase(Dest::CloudsReverb,    raw(Raw::cloudsReverb));
    setBase(Dest::CloudsDryWet,    raw(Raw::cloudsDryWet));

    for (const auto* pp : polyParams_) {
        const int tag = pp->getParamTag();
        if (tag >= 0 && tag < static_cast<int>(Dest::Count))
            base[static_cast<size_t>(tag)] += pp->getMonoModulationOffset();
    }

    ep.clouds.mode         = static_cast<int>(raw(Raw::cloudsMode));
    ep.clouds.freeze       = raw(Raw::cloudsFreeze) > 0.5f;
    ep.clouds.position     = base[static_cast<size_t>(Dest::CloudsPosition)];
    ep.clouds.size         = base[static_cast<size_t>(Dest::CloudsSize)];
    ep.clouds.density      = base[static_cast<size_t>(Dest::CloudsDensity)];
    ep.clouds.texture      = base[static_cast<size_t>(Dest::CloudsTexture)];
    ep.clouds.pitch        = base[static_cast<size_t>(Dest::CloudsPitch)];
    ep.clouds.feedback     = base[static_cast<size_t>(Dest::CloudsFeedback)];
    ep.clouds.reverb       = base[static_cast<size_t>(Dest::CloudsReverb)];
    ep.clouds.stereoSpread = raw(Raw::cloudsSpread);
    ep.clouds.dryWet       = base[static_cast<size_t>(Dest::CloudsDryWet)];

    ep.marbles.enabled       = raw(Raw::marblesEnable) > 0.5f;
    ep.marbles.tempoSync     = raw(Raw::marblesTempoSync) > 0.5f;
    ep.marbles.divisionIndex = static_cast<int>(raw(Raw::marblesDivision));
    ep.marbles.hostBpm       = 120.0; // filled in by processBlock from the transport
    ep.marbles.rateHz        = raw(Raw::marblesRate);
    ep.marbles.jitter        = raw(Raw::marblesJitter);
    ep.marbles.dejaVu        = raw(Raw::marblesDejaVu);
    ep.marbles.loopLength    = static_cast<int>(std::lround(raw(Raw::marblesLength)));
    ep.marbles.spread        = raw(Raw::marblesSpread);
    ep.marbles.steps         = raw(Raw::marblesSteps);

    static constexpr int kVoiceLimits[4] = { 1, 4, 8, 16 };
    ep.maxPolyphony = kVoiceLimits[std::clamp(static_cast<int>(raw(Raw::polyphonyMode)), 0, 3)];
    ep.masterGainLinear = juce::Decibels::decibelsToGain(raw(Raw::masterGain));

    return ep;
}

void KingsIntoClownsProcessor::triggerManualStrum(int midiNote, float velocity) noexcept {
    pendingAuditionNote_.store(midiNote, std::memory_order_relaxed);
    pendingAuditionVel_.store(velocity, std::memory_order_relaxed);
}

void KingsIntoClownsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Host transport: tempo for the loom & synced LFOs, song position to lock LFO phase to the bar
    double hostBpm = 120.0;
    double hostPpq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto bpmOpt = pos->getBpm(); bpmOpt && *bpmOpt > 20.0) hostBpm = *bpmOpt;
            if (auto ppqOpt = pos->getPpqPosition()) {
                hostPpq = *ppqOpt;
                hostPlaying = pos->getIsPlaying();
            }
        }
    } else if (clap_transport != nullptr) {
        if ((clap_transport->flags & CLAP_TRANSPORT_HAS_TEMPO) && clap_transport->tempo > 20.0) hostBpm = clap_transport->tempo;
        if (clap_transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
            hostPpq = static_cast<double>(clap_transport->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
            hostPlaying = (clap_transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        }
    }

    const int auditionNote = pendingAuditionNote_.exchange(-1, std::memory_order_relaxed);
    if (auditionNote >= 0) {
        const float vel = pendingAuditionVel_.load(std::memory_order_relaxed);
        midiMessages.addEvent(juce::MidiMessage::noteOn(1, auditionNote, vel), 0);
        midiMessages.addEvent(juce::MidiMessage::noteOff(1, auditionNote, 0.0f), std::max(0, buffer.getNumSamples() - 1));
    }

    auto engineParams = gatherEngineParameters();
    engineParams.hostBpm = hostBpm;
    engineParams.hostPpqAtBlockStart = hostPpq;
    engineParams.hostPlaying = hostPlaying;
    engineParams.marbles.hostBpm = hostBpm;
    synthEngine_.renderBlock(buffer, midiMessages, engineParams, visualFifo_, &midiLearn_);
}

bool KingsIntoClownsProcessor::voiceInfoGet(clap_voice_info* info) {
    if (!info) return false;
    info->voice_count = 16;
    info->voice_capacity = 16;
    info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
    return true;
}

bool KingsIntoClownsProcessor::supportsDirectEvent(uint16_t space_id, uint16_t type) {
    if (space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    return type == CLAP_EVENT_NOTE_ON
        || type == CLAP_EVENT_NOTE_OFF
        || type == CLAP_EVENT_NOTE_CHOKE
        || type == CLAP_EVENT_NOTE_EXPRESSION;
}

void KingsIntoClownsProcessor::handleDirectEvent(const clap_event_header_t* event, int /*sampleOffset*/) {
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

    switch (event->type) {
        case CLAP_EVENT_NOTE_ON: {
            const auto* ne = reinterpret_cast<const clap_event_note_t*>(event);
            synthEngine_.handleNoteOn(ne->key, static_cast<float>(ne->velocity), ne->channel, ne->note_id);
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE: {
            const auto* ne = reinterpret_cast<const clap_event_note_t*>(event);
            synthEngine_.handleNoteOff(ne->key, static_cast<float>(ne->velocity), ne->channel, ne->note_id);
            break;
        }
        case CLAP_EVENT_NOTE_EXPRESSION: {
            const auto* expr = reinterpret_cast<const clap_event_note_expression_t*>(event);
            synthEngine_.handleClapNoteExpression(expr->note_id, expr->channel, expr->key,
                                                 static_cast<int>(expr->expression_id), expr->value);
            break;
        }
        default:
            break;
    }
}

void KingsIntoClownsProcessor::addOutboundEventsToQueue(const clap_output_events* out_events,
                                                        const juce::MidiBuffer& /*midiBuffer*/,
                                                        int sampleOffset) {
    if (!out_events) {
        synthEngine_.clearTerminatedNotes();
        return;
    }

    for (const auto& ended : synthEngine_.getTerminatedNotes()) {
        clap_event_note_t noteEnd{};
        noteEnd.header.size = sizeof(clap_event_note_t);
        noteEnd.header.time = ended.sampleOffset + static_cast<uint32_t>(std::max(0, sampleOffset));
        noteEnd.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        noteEnd.header.type = CLAP_EVENT_NOTE_END;
        noteEnd.header.flags = 0;
        noteEnd.note_id = ended.noteId;
        noteEnd.port_index = ended.portIndex;
        noteEnd.channel = ended.channel;
        noteEnd.key = ended.key;
        noteEnd.velocity = 0.0;
        out_events->try_push(out_events, &noteEnd.header);
    }
    synthEngine_.clearTerminatedNotes();
}

int KingsIntoClownsProcessor::getNumPrograms() {
    return presetManager_ ? presetManager_->getNumPresets() : 1;
}

int KingsIntoClownsProcessor::getCurrentProgram() {
    return presetManager_ ? std::max(0, presetManager_->getCurrentIndex()) : 0;
}

void KingsIntoClownsProcessor::setCurrentProgram(int index) {
    if (presetManager_) presetManager_->loadPreset(index);
}

const juce::String KingsIntoClownsProcessor::getProgramName(int index) {
    if (presetManager_ && index >= 0 && index < presetManager_->getNumPresets())
        return presetManager_->getAllPresets()[static_cast<size_t>(index)].name;
    return {};
}

void KingsIntoClownsProcessor::changeProgramName(int /*index*/, const juce::String& /*newName*/) {}

juce::String KingsIntoClownsProcessor::getCurrentPoemSubtitle() const {
    if (presetManager_) {
        if (const auto* p = presetManager_->getCurrentPreset())
            return p->poemLine;
    }
    return "";
}

int KingsIntoClownsProcessor::getSavedEditorWidth() const {
    return static_cast<int>(apvts_.state.getProperty("uiWidth", 0));
}

void KingsIntoClownsProcessor::setSavedEditorWidth(int width) {
    apvts_.state.setProperty("uiWidth", width, nullptr);
}

void KingsIntoClownsProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    state.setProperty("presetIndex", presetManager_ ? presetManager_->getCurrentIndex() : 0, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    if (presetManager_) {
        auto* modXml = xml->createNewChildElement("ModMatrix");
        presetManager_->saveModMatrixToXml(modXml);
    }
    if (auto midiXml = midiLearn_.saveToXml()) {
        xml->addChildElement(midiXml.release());
    }

    copyXmlToBinary(*xml, destData);
}

void KingsIntoClownsProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts_.state.getType())) {
        if (auto* modXml = xmlState->getChildByName("ModMatrix")) {
            if (presetManager_) presetManager_->loadModMatrixFromXml(modXml);
        }
        if (auto* midiXml = xmlState->getChildByName("MidiLearnMappings")) {
            midiLearn_.loadFromXml(midiXml);
        }
        auto vt = juce::ValueTree::fromXml(*xmlState);
        apvts_.replaceState(vt);
        if (presetManager_)
            presetManager_->setCurrentIndexSilently(static_cast<int>(vt.getProperty("presetIndex", 0)));
    }
}

juce::AudioProcessorEditor* KingsIntoClownsProcessor::createEditor() {
    return new KingsIntoClownsEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new KingsIntoClownsProcessor();
}
