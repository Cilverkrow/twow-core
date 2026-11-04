#pragma once

#include <cstdint>

namespace ai::combat_stuck_clock
{
// twow-repo#575 / #601 (OB-00 04.11.2026; evidence ws-10/cli-pathfinding/movement-a3/ROOT-HEARTH-FARMOVE.md §4):
// AiPlayerbot.Unstuck.CombatFightClock. "combat stuck" (5 min -> unstuck -> reset, Reset(true) clears the travel target)
// and "combat long stuck" (15 min -> unstuck -> hearthstone / repop) measured the "time since last change" of the
// value "combat::self target". That value only notices a change when it is read, and both triggers read it only in
// combat - so the end of a fight was usually never seen and the next fight started with a clock that had run since an
// earlier fight. cand4G u2: 390 "combat long stuck" hearthstones, 2 of them in a fight running > 60 s; run T (rot01-tank,
// decision trace): 1,136 "combat (long) stuck" triggers, none in a fight running >= 60 s.
// With the switch the clock is the fight with the current target: it starts at the combat start ("combat start time",
// set in PlayerbotAI::OnCombatStarted) and restarts on every change of the current target. Pure rules: no game types,
// no locks, no config reads (t/combat_stuck_clock_policy_tests.cpp).

// A check more than this many seconds after the previous check in combat belongs to a new fight (the triggers are
// checked every combat tick; a gap means the bot left combat or was not active - no reason to count it as stuck).
constexpr std::int64_t NewFightGapSeconds = 30;

struct Clock
{
    std::int64_t anchor = 0;       // start of the fight this clock counts (0 = no fight seen yet)
    std::int64_t startValue = 0;   // the "combat start time" the anchor was taken from (0 = unknown start)
    std::int64_t lastSeen = 0;     // time of the previous check in combat
    std::uint64_t target = 0;      // raw guid of the current target at the previous check (0 = none)
    std::int64_t targetSince = 0;  // when that target was first seen in this fight
};

// Seconds the bot has spent in the current fight with its current target. Called only while the bot is in combat.
// now: time(nullptr). combatStart: the value "combat start time" (0 = unknown). target: raw guid of "current target"
// (0 = none). A start value that is new and not in the future is the start of this fight (taken at the latest at the
// previous check: a start value set while the clock was already running cannot move the anchor back); an unknown or
// stale start value (unchanged across a gap) starts the fight now - fail safe, it can only make the clock shorter.
inline std::uint32_t Seconds(Clock& c, std::int64_t now, std::int64_t combatStart, std::uint64_t target)
{
    bool const freshStart = combatStart > 0 && combatStart <= now && combatStart != c.startValue;
    bool const newFight = c.anchor == 0 || now - c.lastSeen > NewFightGapSeconds || freshStart;
    if (newFight)
    {
        c.anchor = freshStart ? (combatStart > c.lastSeen ? combatStart : c.lastSeen) : now;
        c.startValue = combatStart;
        c.target = target;
        c.targetSince = c.anchor;
    }
    else if (target != c.target)
    {
        c.target = target;
        c.targetSince = now;
    }
    c.lastSeen = now;
    std::int64_t const from = c.targetSince > c.anchor ? c.targetSince : c.anchor;
    return now > from ? std::uint32_t(now - from) : 0u;
}
}
