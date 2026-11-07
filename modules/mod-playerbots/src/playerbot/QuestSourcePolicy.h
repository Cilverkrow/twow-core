#pragma once

#include "PlannerPolicy.h"

#include <cstddef>
#include <cstdint>

// A3 step 2, quest book source choice and order (QB-S: QB1 + QB3 + side XP; ADR-0041, OB-00 07.11.2026;
// AiPlayerbot.QuestBook.SourceChoice, default 0, acts only where the quest book is active). Plan:
// ws-10/cli-quest-organic/a3/step2-questbook/QB-BUILD-PLAN-20261107.md, section 7. Pure rules: numbers in, numbers out. No game
// types, no config reads, no locks, no log calls; the callers snapshot the facts (static source stats at load, per-bot needs on
// the bot thread) and read them here, also in the async travel search.
//
// QB1: among the sources (creature / game object entries) of one quest objective, the bot goes to the one with the least
// expected time = route + work. Work = the kills it takes (needed / drop chance) x (kill time + spawn spacing / run speed), and
// never less than the respawn cycles when the source has fewer spawns than kills. A named or single spawn wins only when it is
// better by the margin (a camped rare is a trap more often than a shortcut).
// QB3: the winners of all quests are ordered by planner::XpPerMin over (quest xp + follow-up xp + side xp of the kills).
namespace ai::questsource
{
struct Params
{
    float killSec = 12.0f;        // one kill incl. loot (QuestBook.KillSeconds)
    float singleMargin = 1.5f;    // a named / single source must be this much cheaper (QuestBook.SingleSpawnMargin)
    float minSpeed = 1.0f;        // yards per second floor (a rooted or zero speed)
};

// The static facts of one source, filled once at load.
struct Stats
{
    std::uint16_t spawns = 0;       // over all maps; 0 = unknown (no respawn bound)
    std::uint16_t respawnSec = 0;   // the shortest respawn of its spawns
    std::uint8_t chancePct = 100;   // drop chance of the quest item; a kill objective has 100; 0 = unknown -> 100
    std::uint16_t spacingYd = 0;    // mean nearest-neighbour distance of the spawns
    bool named = false;             // at most two spawns, or rank rare and up
    bool killTarget = false;        // the entry is the objective's kill / use target (not an item source)
    bool vendor = false;            // an item objective bought from this vendor: one visit, no kills
    std::uint8_t mobLevel = 0;      // creature template level (mean of min and max); 0 = a game object, no kill XP
};

inline float NonNegative(float v)
{
    return v > 0.0f ? v : 0.0f;   // NaN and negatives become 0
}

// The kills it takes to collect `needed` items at chancePct per kill (rounded up). A kill objective passes 100.
inline std::uint32_t Kills(std::uint32_t needed, std::uint8_t chancePct)
{
    if (!needed)
        return 0;
    std::uint32_t const chance = chancePct == 0 || chancePct > 100 ? 100u : chancePct;
    return std::uint32_t((std::uint64_t(needed) * 100u + chance - 1u) / chance);
}

inline float Speed(float speed, Params const& p)
{
    float const floor = p.minSpeed > 0.0f ? p.minSpeed : 1.0f;
    return speed > floor ? speed : floor;
}

// Expected work at the source in seconds (the approach excluded).
inline float WorkSec(std::uint32_t needed, Stats const& s, float speed, Params const& p)
{
    if (s.vendor)
        return needed ? NonNegative(p.killSec) : 0.0f;   // one visit buys them all
    std::uint32_t const kills = Kills(needed, s.chancePct);
    if (!kills)
        return 0.0f;
    float const perKill = NonNegative(p.killSec) + float(s.spacingYd) / Speed(speed, p);
    float work = float(kills) * perKill;
    if (s.spawns && kills > s.spawns)
    {
        std::uint32_t const cycles = (kills + s.spawns - 1u) / s.spawns - 1u;
        float const waitBound = float(cycles) * float(s.respawnSec);
        if (waitBound > work)
            work = waitBound;
    }
    return work;
}

inline float RouteSec(float distanceYd, float speed, Params const& p)
{
    return NonNegative(distanceYd) / Speed(speed, p);
}

// One source option of an objective, for the pick.
struct Option
{
    float cost = 0.0f;   // route + work
    bool named = false;
    bool valid = false;
};

// The index of the chosen source, or -1 when no option is valid. The cheapest common source wins, unless the cheapest named one
// is cheaper by the margin (cost x margin < common cost). Ties keep the earlier option.
inline int PickSource(Option const* options, std::size_t n, Params const& p)
{
    int common = -1;
    int named = -1;
    for (std::size_t i = 0; options && i < n; ++i)
    {
        Option const& o = options[i];
        if (!o.valid)
            continue;
        int& best = o.named ? named : common;
        if (best < 0 || o.cost < options[best].cost)
            best = int(i);
    }
    if (common < 0)
        return named;
    if (named < 0)
        return common;
    float const margin = p.singleMargin > 1.0f ? p.singleMargin : 1.0f;
    return options[named].cost * margin < options[common].cost ? named : common;
}

// T3b: the XP of the kills at the source (each kill gives mob XP, for a kill and for an item objective alike).
inline float SideXp(std::uint32_t kills, std::uint32_t mobXp)
{
    return float(kills) * float(mobXp);
}

// QB3: the order score of one objective winner, XP per minute as the planner counts it.
inline float Score(float questXp, float followUpXp, float sideXp, float routeSec, float workSec)
{
    planner::Candidate c;
    c.kind = planner::Kind::QuestObjective;
    c.xp = NonNegative(questXp) + NonNegative(sideXp);
    c.followUpXp = followUpXp;
    c.routeSec = routeSec;
    c.workSec = workSec;
    return planner::XpPerMin(c, planner::Weights{});
}
}
