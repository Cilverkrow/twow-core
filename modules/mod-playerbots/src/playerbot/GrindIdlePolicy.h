#pragma once

#include <cstdint>

// twow-repo#601 (AiPlayerbot.ActivityTrace.GrindIdleDetail, OB-00 20.11.2026, deep research 1 NOTE-R1 section 4 no. 3): in
// DAUER-B the L11-20 roster bots spent 21 % of their time as grind_idle - at a grind destination (WORK) without a single fight
// in the sample - and no log said why. Three causes are named: crowding (other players take the creatures), grey spots (the
// grind window of GrindTravelDestination::IsPossible, TravelMgr.cpp, lets a low-durability bot pick creatures at or below its
// grey level, which the grind target filter then skips) and waiting for a respawn (nothing alive left at the spot). With the
// switch a grind_idle episode (two samples in a row) writes one start line with the bot's last grind target pass (how many
// candidates, why each was refused, whether one was picked), the destination's creature levels against the grey level and the
// friendly players in sight, plus the cause this header derives from them; its end writes the seconds. Pure, no lock.
namespace ai::grind_idle
{
// Why the grind target pass refused a candidate (GrindTargetValue::FindTargetForGrinding, in the order of its checks).
enum class Skip : std::uint8_t
{
    Unreachable = 0,   // this bot could not reach it (Hotfix 8.2, five minutes)
    Height,         // too far above/below
    FreeRange,      // out of free range / far from master
    LevelAbove,     // too many levels above the bot
    Avoided,        // killed this bot repeatedly (#307)
    Elite,          // elite while it cannot fight elites
    Invalid,        // pet, evading, unkillable
    Tapped,         // tapped by another player, crowd-controlled or out of range (PossibleAttackTargetsValue::IsPossibleTarget)
    Critter,        // critter
    Rule,           // GrindMode 2, QuestFirstOrder, RelationWorkNoGrind, GrindMode 1 window
    NotNeeded,      // the 20 % "not needed for active quest" skip on a non-grind trip
    Grey,           // not XP and not needed for a quest (grey by level for a roster bot on its own)
    Count
};

constexpr std::uint8_t SkipCount = std::uint8_t(Skip::Count);

// The bot's newest grind target pass (round 0, the bot's own pick; assist rounds are not counted). Written and read on the
// bot's own update thread only.
struct PassTally
{
    std::uint32_t at = 0;            // time of the pass, 0 = none yet
    std::uint16_t candidates = 0;    // "possible targets" entries the loop looked at (a unit that is gone is no candidate)
    std::uint16_t skips[SkipCount] = {};
    std::uint16_t crowded = 0;       // kept candidates already targeted by more bots than the assist round allows
    bool picked = false;             // the pass returned a target (an attacker too)

    void Start(std::uint32_t now)
    {
        *this = PassTally();
        at = now;
    }

    void Note(Skip why)
    {
        std::uint16_t& n = skips[std::uint8_t(why)];
        if (n < 0xFFFF)
            ++n;
    }
};

enum class Cause : std::uint8_t
{
    NoPass = 0,   // no grind target pass in the last PassMaxAgeSeconds: the grind action did not look for a target
    Picked,       // the last pass picked a target and still no fight (reach, path, the target died first)
    Respawn,      // no candidate and fewer than CrowdOthers friendly players in sight: nothing alive at the spot
    Crowded,      // no candidate with CrowdOthers or more players in sight, or tapped refusals are the larger half
    Grey,         // grey refusals are at least half of the candidates
    Rules,        // quest-first rules (Rule, NotNeeded) are at least half of the candidates
    Other         // anything else (height, range, level, elite, invalid, critters, avoided)
};

constexpr std::uint32_t PassMaxAgeSeconds = 120;
constexpr std::uint32_t CrowdOthers = 2;

// Order of the tests: a stale or missing pass first, then a pick, then an empty candidate list (respawn vs crowded by the players
// in sight), then the refusal that covers at least half of the candidates: grey, tapped (crowded), the quest-first rules.
inline Cause CauseOf(PassTally const& t, std::uint32_t now, std::uint32_t others)
{
    if (!t.at || now < t.at || now - t.at > PassMaxAgeSeconds)
        return Cause::NoPass;
    if (t.picked)
        return Cause::Picked;
    if (!t.candidates)
        return others >= CrowdOthers ? Cause::Crowded : Cause::Respawn;
    std::uint32_t const half = (std::uint32_t(t.candidates) + 1) / 2;
    if (t.skips[std::uint8_t(Skip::Grey)] >= half)
        return Cause::Grey;
    if (t.skips[std::uint8_t(Skip::Tapped)] >= half)
        return Cause::Crowded;
    if (std::uint32_t(t.skips[std::uint8_t(Skip::Rule)]) + t.skips[std::uint8_t(Skip::NotNeeded)] >= half)
        return Cause::Rules;
    return Cause::Other;
}

inline char const* CauseName(Cause c)
{
    switch (c)
    {
        case Cause::NoPass: return "no_pass";
        case Cause::Picked: return "picked";
        case Cause::Respawn: return "respawn";
        case Cause::Crowded: return "crowded";
        case Cause::Grey: return "grey";
        case Cause::Rules: return "rules";
        default: return "other";
    }
}

// The destination's creatures are grey for the bot (the core's colour: level <= grey level) - a grey spot whatever the pass
// found. maxLevel 0 = unknown template.
inline bool GreySpot(std::uint32_t templateMaxLevel, std::uint32_t greyLevel)
{
    return templateMaxLevel && templateMaxLevel <= greyLevel;
}
}
