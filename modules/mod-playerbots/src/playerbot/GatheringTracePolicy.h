#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::gathering_trace
{
// twow-repo#333 T28-M (plan A3-PLAN-ZIEL T28, OB-00 09.11.2026; AiPlayerbot.Gathering.Trace, 0 off by default, 1 on): a pure
// measurement of the time a roster bot on its own spends on its gathering professions. Per bot one window per online hour;
// at its end one line
//   [Gathering] event=share bot= level= play_s= purpose_s= trip_s= cast_s= opens_herb= opens_ore= opens_skin= crafts=
//     s182=<value>/<max> s186=<value>/<max> s393=<value>/<max>
// Every counter is the increment of its window. No game types, no config, no logging here: the caller samples the bot,
// notes the casts it starts and writes the line this header formats.
//
// Time accounting (left-point sampling): each sample credits the seconds since the previous sample to the activity seen at
// the previous sample. The sampler runs every SampleSeconds while the engine checks triggers, so a start or end is off by at
// most one sample gap (the purpose end itself is only seen by the 60 s progress check). Overlap rule: a profession trip
// that is the active travel target wins over a declared gathering purpose - the bot walks to a trainer or vendor then, not
// to a node - so purpose_s and trip_s never count the same second and purpose_s + trip_s <= play_s.
constexpr uint32_t WindowSeconds = 3600;
constexpr uint32_t SampleSeconds = 5;   // check interval of the "gathering trace" trigger

// The gathering casts OpenLootAction starts (LootAction.cpp: HERB_GATHERING, MINING, SKINNING; creature herb 32605 / ore 32606).
constexpr uint32_t kHerbGatheringSpell = 2366;
constexpr uint32_t kMiningSpell = 2575;
constexpr uint32_t kSkinningSpell = 8613;
constexpr uint32_t kCreatureHerbSpell = 32605;
constexpr uint32_t kCreatureOreSpell = 32606;

enum class OpenKind : uint8_t
{
    None,
    Herb,
    Ore,
    Skin
};

// The kind of a gathering open by its spell; None for every other spell (lockpicking, chests, quest objects, engineering).
inline OpenKind OpenKindOfSpell(uint32_t spellId)
{
    switch (spellId)
    {
        case kHerbGatheringSpell:
        case kCreatureHerbSpell:
            return OpenKind::Herb;
        case kMiningSpell:
        case kCreatureOreSpell:
            return OpenKind::Ore;
        case kSkinningSpell:
            return OpenKind::Skin;
        default:
            return OpenKind::None;
    }
}

enum class Activity : uint8_t
{
    None,
    Purpose,
    Trip
};

// The overlap rule: a trip outranks a purpose.
inline Activity ActivityOf(bool purposeActive, bool tripActive)
{
    if (tripActive)
        return Activity::Trip;
    return purposeActive ? Activity::Purpose : Activity::None;
}

struct Skill
{
    uint32_t value = 0;
    uint32_t max = 0;
};

struct Window
{
    uint32_t start = 0;        // 0 = no window yet (the first sample opens one)
    uint32_t lastSample = 0;
    Activity lastActivity = Activity::None;
    uint32_t purposeSeconds = 0;
    uint32_t tripSeconds = 0;
    uint64_t castMs = 0;
    uint32_t opensHerb = 0;
    uint32_t opensOre = 0;
    uint32_t opensSkin = 0;
    uint32_t crafts = 0;

    bool Open() const { return start != 0; }

    // Out of scope (or never sampled): everything goes, the next sample opens a fresh window.
    void Reset() { *this = Window(); }

    // A gathering open the bot started; only inside an open window, other spells are not counted.
    void NoteOpen(OpenKind kind, uint32_t castDurationMs)
    {
        if (!Open() || kind == OpenKind::None)
            return;
        if (kind == OpenKind::Herb)
            ++opensHerb;
        else if (kind == OpenKind::Ore)
            ++opensOre;
        else
            ++opensSkin;
        castMs += castDurationMs;
    }

    // A profession craft whose cast started; only inside an open window.
    void NoteCraft(uint32_t castDurationMs)
    {
        if (!Open())
            return;
        ++crafts;
        castMs += castDurationMs;
    }

    // One sample at now. Credits the seconds since the previous sample to the previous activity and keeps the current one
    // for the next interval. true = the window has lasted WindowSeconds: write the line, then Roll(). A clock that went back
    // counts as no time.
    bool Sample(uint32_t now, bool purposeActive, bool tripActive)
    {
        Activity const current = ActivityOf(purposeActive, tripActive);
        if (!Open())
        {
            start = lastSample = now;
            lastActivity = current;
            return false;
        }
        if (now < lastSample)
            now = lastSample;
        uint32_t const elapsed = now - lastSample;
        if (lastActivity == Activity::Trip)
            tripSeconds += elapsed;
        else if (lastActivity == Activity::Purpose)
            purposeSeconds += elapsed;
        lastSample = now;
        lastActivity = current;
        return now - start >= WindowSeconds;
    }

    uint32_t PlaySeconds() const { return lastSample - start; }
    uint32_t CastSeconds() const { return uint32_t((castMs + 500) / 1000); }

    // After the line: the next window starts at the last sample, with the activity seen there; the counters start at 0.
    void Roll()
    {
        uint32_t const at = lastSample;
        Activity const activity = lastActivity;
        Reset();
        start = lastSample = at;
        lastActivity = activity;
    }
};

// The line, field order fixed (the format above).
inline std::string ShareLine(uint32_t bot, uint32_t level, Window const& window, Skill herbalism, Skill mining, Skill skinning)
{
    char buffer[384];
    std::snprintf(buffer, sizeof(buffer),
        "[Gathering] event=share bot=%u level=%u play_s=%u purpose_s=%u trip_s=%u cast_s=%u opens_herb=%u opens_ore=%u "
        "opens_skin=%u crafts=%u s182=%u/%u s186=%u/%u s393=%u/%u",
        unsigned(bot), unsigned(level), unsigned(window.PlaySeconds()), unsigned(window.purposeSeconds),
        unsigned(window.tripSeconds), unsigned(window.CastSeconds()), unsigned(window.opensHerb), unsigned(window.opensOre),
        unsigned(window.opensSkin), unsigned(window.crafts), unsigned(herbalism.value), unsigned(herbalism.max),
        unsigned(mining.value), unsigned(mining.max), unsigned(skinning.value), unsigned(skinning.max));
    return buffer;
}
}
