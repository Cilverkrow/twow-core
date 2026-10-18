#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ai::grind_pick
{
// twow-repo#601 S2 diagnostic (OB-10 / OB-00 18.10.2026, AiPlayerbot.BotUpdateTrace, no own key): where the bots' grind
// targets come from. The phase-1 grey share (L15-19 37.9 %) counts AttackAnythingAction targets, picked near the bot by
// GrindTargetValue - not only at grind destinations - so before S2 changes the grind window this splits every new grind
// target into its source and whether it was grey (no XP): an attacker of the bot or its group, a creature already on
// the bot, a creature needed for an active quest, or a plain grind pick. One [GrindPick] line per 5-level band and minute.
enum class Source : std::uint8_t
{
    None = 0,
    Attacker,   // "possible attack targets": attacks the bot or its group (self-defence, no grey test)
    Victim,     // picked from "possible targets" while its victim is the bot
    Quest,      // needed for an active quest
    Grind,      // everything else (opportunistic / grind destination)
    Count
};

constexpr std::size_t SourceCount = std::size_t(Source::Count);
constexpr std::size_t Bands = 12;   // L1-5, L6-10, ..., L56-60 (above 60 in the last band)

inline std::size_t BandOf(std::uint32_t level)
{
    std::size_t const band = level ? std::size_t((level - 1) / 5) : 0;
    return band < Bands ? band : Bands - 1;
}

inline char const* SourceName(Source source)
{
    switch (source)
    {
        case Source::Attacker: return "attacker";
        case Source::Victim: return "victim";
        case Source::Quest: return "quest";
        case Source::Grind: return "grind";
        default: return "none";
    }
}

// The source of a target picked from "possible targets" (the attacker loop is Attacker before this is asked).
inline Source PickedSource(bool victimIsBot, bool neededForQuest)
{
    return victimIsBot ? Source::Victim : neededForQuest ? Source::Quest : Source::Grind;
}

struct Snapshot
{
    std::array<std::array<std::uint32_t, SourceCount>, Bands> picks{};
    std::array<std::array<std::uint32_t, SourceCount>, Bands> grey{};
};

// Any thread (the bots' map threads add, the world thread takes once a minute). Relaxed atomics only, no lock.
class Counters
{
public:
    void Add(std::size_t band, Source source, bool grey)
    {
        if (band >= Bands || source == Source::None || source >= Source::Count)
            return;
        picks[band][std::size_t(source)].fetch_add(1, std::memory_order_relaxed);
        if (grey)
            greys[band][std::size_t(source)].fetch_add(1, std::memory_order_relaxed);
    }

    Snapshot Take()
    {
        Snapshot s;
        for (std::size_t b = 0; b < Bands; ++b)
            for (std::size_t i = 0; i < SourceCount; ++i)
            {
                s.picks[b][i] = picks[b][i].exchange(0, std::memory_order_relaxed);
                s.grey[b][i] = greys[b][i].exchange(0, std::memory_order_relaxed);
            }
        return s;
    }

private:
    std::array<std::array<std::atomic<std::uint32_t>, SourceCount>, Bands> picks{};
    std::array<std::array<std::atomic<std::uint32_t>, SourceCount>, Bands> greys{};
};

inline Counters& Global()
{
    static Counters counters;
    return counters;
}

inline bool BandEmpty(Snapshot const& s, std::size_t band)
{
    for (std::size_t i = 1; i < SourceCount; ++i)
        if (s.picks[band][i])
            return false;
    return true;
}

// "[GrindPick] band=L15-19 attacker=12/3 victim=2/0 quest=40/9 grind=55/1" - picks/grey per source.
inline std::string FormatLine(Snapshot const& s, std::size_t band)
{
    std::string line = "[GrindPick] band=L" + std::to_string(band * 5 + 1) + "-" + std::to_string(band * 5 + 5);
    for (std::size_t i = 1; i < SourceCount; ++i)
        line += std::string(" ") + SourceName(Source(i)) + "=" + std::to_string(s.picks[band][i]) + "/" +
            std::to_string(s.grey[band][i]);
    return line;
}
}
