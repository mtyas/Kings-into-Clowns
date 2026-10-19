#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace ric::mod {

enum class Source : uint8_t {
    LFO1 = 0,
    LFO2,
    LFO3,
    ENV1,
    ENV2,
    ENV3,
    LOOM,
    Count
};

enum class Dest : uint8_t {
    // I. Voice & Exciter
    OscTune = 0,
    OscHarmonics,
    OscTimbre,
    OscMorph,
    SubBlend,
    StrikeLevel,
    DirectMix,
    // II. Resonator Body (Rings)
    RingsStructure,
    RingsBrightness,
    RingsDamping,
    RingsPosition,
    RingsTune,
    // III. Cloud & Space (Clouds + Oliverb)
    CloudsPosition,
    CloudsSize,
    CloudsDensity,
    CloudsTexture,
    CloudsPitch,
    CloudsFeedback,
    CloudsReverb,
    CloudsDryWet,
    Count
};

inline juce::String getSourceName(Source s) {
    switch (s) {
        case Source::LFO1: return "LFO 1";
        case Source::LFO2: return "LFO 2";
        case Source::LFO3: return "LFO 3";
        case Source::ENV1: return "ENV 1";
        case Source::ENV2: return "ENV 2";
        case Source::ENV3: return "ENV 3";
        case Source::LOOM: return "LOOM";
        default:           return "None";
    }
}

inline juce::String getDestName(Dest d) {
    switch (d) {
        case Dest::OscTune:         return "Voice Tune";
        case Dest::OscHarmonics:    return "Harmonics";
        case Dest::OscTimbre:       return "Timbre";
        case Dest::OscMorph:        return "Morph";
        case Dest::SubBlend:        return "Sub Layer";
        case Dest::StrikeLevel:     return "Mallet Strike";
        case Dest::DirectMix:       return "Direct Mix";
        case Dest::RingsStructure:  return "Structure";
        case Dest::RingsBrightness: return "Brightness";
        case Dest::RingsDamping:    return "Damping";
        case Dest::RingsPosition:   return "Position";
        case Dest::RingsTune:       return "Body Tune";
        case Dest::CloudsPosition:  return "Cloud Position";
        case Dest::CloudsSize:      return "Grain Size";
        case Dest::CloudsDensity:   return "Density";
        case Dest::CloudsTexture:   return "Texture";
        case Dest::CloudsPitch:     return "Cloud Pitch";
        case Dest::CloudsFeedback:  return "Feedback";
        case Dest::CloudsReverb:    return "Reverb";
        case Dest::CloudsDryWet:    return "Cloud Mix";
        default:                    return "None";
    }
}

inline bool isPitchDestination(Dest d) noexcept {
    return d == Dest::OscTune || d == Dest::RingsTune || d == Dest::CloudsPitch;
}

struct ModSlot {
    Source source = Source::LFO1;
    Dest dest = Dest::OscTimbre;
    float amount = 0.0f;
    bool enabled = false;
};

// Slots are packed into single 64-bit atomics so the UI thread can edit routings
// while the audio thread evaluates them, without locks and without torn reads.
class ModMatrix {
public:
    static constexpr size_t kMaxSlots = 32;
    static constexpr size_t kNumSources = static_cast<size_t>(Source::Count);
    static constexpr size_t kNumDests = static_cast<size_t>(Dest::Count);

    ModMatrix() noexcept {
        initDefaultPoeticRouting();
    }

    void clearAll() noexcept {
        for (auto& s : slots_) s.store(pack({}), std::memory_order_relaxed);
        bumpVersion();
    }

    void initDefaultPoeticRouting() noexcept {
        clearAll();
        // Gentle default motion so the live modulation orbits are visible straight away
        setSlot(0, Source::LFO1, Dest::OscMorph,        0.22f, true);
        setSlot(1, Source::LFO2, Dest::RingsPosition,   0.25f, true);
        setSlot(2, Source::LFO3, Dest::CloudsPosition,  0.20f, true);
        setSlot(3, Source::ENV2, Dest::OscTimbre,       0.30f, true);
        setSlot(4, Source::ENV2, Dest::RingsBrightness, 0.25f, true);
        setSlot(5, Source::LOOM, Dest::RingsStructure,  0.25f, true);
    }

    void setSlot(size_t index, Source src, Dest dst, float amount, bool enabled = true) noexcept {
        if (index >= kMaxSlots) return;
        slots_[index].store(pack({ src, dst, std::clamp(amount, -1.0f, 1.0f), enabled }), std::memory_order_relaxed);
        bumpVersion();
    }

    ModSlot getSlot(size_t index) const noexcept {
        return index < kMaxSlots ? unpack(slots_[index].load(std::memory_order_relaxed)) : ModSlot{};
    }

    std::array<ModSlot, kMaxSlots> getSlots() const noexcept {
        std::array<ModSlot, kMaxSlots> out{};
        for (size_t i = 0; i < kMaxSlots; ++i) out[i] = getSlot(i);
        return out;
    }

    void assignOrUpdateConnection(Source src, Dest dst, float amount) noexcept {
        const bool silent = std::abs(amount) < 0.005f;
        for (size_t i = 0; i < kMaxSlots; ++i) {
            const auto s = getSlot(i);
            if (s.enabled && s.source == src && s.dest == dst) {
                setSlot(i, src, dst, silent ? 0.0f : amount, !silent);
                return;
            }
        }
        if (silent) return;

        for (size_t i = 0; i < kMaxSlots; ++i) {
            if (!getSlot(i).enabled) {
                setSlot(i, src, dst, amount, true);
                return;
            }
        }
        setSlot(kMaxSlots - 1, src, dst, amount, true);
    }

    float getConnectionAmount(Source src, Dest dst) const noexcept {
        for (size_t i = 0; i < kMaxSlots; ++i) {
            const auto s = getSlot(i);
            if (s.enabled && s.source == src && s.dest == dst)
                return s.amount;
        }
        return 0.0f;
    }

    float getTotalDepthForDest(Dest dst) const noexcept {
        float depth = 0.0f;
        for (size_t i = 0; i < kMaxSlots; ++i) {
            const auto s = getSlot(i);
            if (s.enabled && s.dest == dst) depth += std::abs(s.amount);
        }
        return depth;
    }

    void clearConnectionsForDest(Dest dst) noexcept {
        for (size_t i = 0; i < kMaxSlots; ++i) {
            const auto s = getSlot(i);
            if (s.enabled && s.dest == dst) setSlot(i, s.source, s.dest, 0.0f, false);
        }
    }

    void clearConnectionsForSource(Source src) noexcept {
        for (size_t i = 0; i < kMaxSlots; ++i) {
            const auto s = getSlot(i);
            if (s.enabled && s.source == src) setSlot(i, s.source, s.dest, 0.0f, false);
        }
    }

    // Incremented on every edit so the UI can cheaply notice routing changes
    uint32_t getVersion() const noexcept { return version_.load(std::memory_order_relaxed); }

    void evaluate(const std::array<float, kNumSources>& sources,
                  const std::array<float, kNumDests>& baseParams,
                  const std::array<float, kNumDests>& clapPolyOffsets,
                  std::array<float, kNumDests>& outFinal) const noexcept {
        for (size_t d = 0; d < kNumDests; ++d) {
            outFinal[d] = baseParams[d] + clapPolyOffsets[d];
        }

        for (size_t i = 0; i < kMaxSlots; ++i) {
            const auto slot = getSlot(i);
            if (!slot.enabled) continue;
            const float srcVal = sources[static_cast<size_t>(slot.source)];
            // Pitch destinations span [-24..+24] semitones, so +/-1.0 modulation = +/-24 semitones
            const float scale = isPitchDestination(slot.dest) ? 24.0f : 1.0f;
            outFinal[static_cast<size_t>(slot.dest)] += srcVal * slot.amount * scale;
        }
    }

private:
    static uint64_t pack(const ModSlot& s) noexcept {
        uint32_t amountBits = 0;
        std::memcpy(&amountBits, &s.amount, sizeof(amountBits));
        return static_cast<uint64_t>(amountBits)
             | (static_cast<uint64_t>(s.source) << 32)
             | (static_cast<uint64_t>(s.dest) << 40)
             | (static_cast<uint64_t>(s.enabled ? 1u : 0u) << 48);
    }

    static ModSlot unpack(uint64_t v) noexcept {
        ModSlot s;
        const auto amountBits = static_cast<uint32_t>(v & 0xFFFFFFFFu);
        std::memcpy(&s.amount, &amountBits, sizeof(amountBits));
        s.source = static_cast<Source>(std::min<uint64_t>((v >> 32) & 0xFFu, kNumSources - 1));
        s.dest = static_cast<Dest>(std::min<uint64_t>((v >> 40) & 0xFFu, kNumDests - 1));
        s.enabled = ((v >> 48) & 1u) != 0;
        return s;
    }

    void bumpVersion() noexcept { version_.fetch_add(1, std::memory_order_relaxed); }

    std::array<std::atomic<uint64_t>, kMaxSlots> slots_{};
    std::atomic<uint32_t> version_{ 0 };
};

} // namespace ric::mod
