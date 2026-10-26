#pragma once

#include <cstdint>

// Run robustness (CLI-GRP, G1 dry-r3 25.10.2026 - Stockade, 1/5 bosses, run disabled "Left the dungeon"). Pure rules, no
// game types; the three keys are DcSettings rows, default 0 = the behaviour before.
namespace DcRunRobust
{
// DungeonClear.SuppressDeathLoopEvac: the stock playerbot death loop (AiPlayerbot.DeathLoop.*: 4 deaths in 15 min at one
// spot) "repop"s a bot - revives it at its race start point and removes it from the group. In a run that ended the run
// (the tank: DcStrategyGate strip -> DisableDungeonClear "Left the dungeon") or lost a member to a new instance copy (a
// follower teleported back by the geo fence after it left the group). With the key the death-loop history of a bot alive
// in the dungeon of an active run is cleared, so deaths between resurrections never add up to an evacuation.
constexpr bool ClearDeathLoop(bool on, bool alive, bool inDungeon, bool runActive)
{
    return on && alive && inDungeon && runActive;
}

// DungeonClear.StayDeadInRun (g1n 26.10.2026: every death in Frostmane released the spirit to the graveyard OUTSIDE the
// instance, and the tank's death ended the run - strip "Left the dungeon"): a dead member of an active run, or of a
// `.dc test` party (externally managed - the run flag may already be cleared after a wipe), stays a corpse in the dungeon
// for the healer's post-combat rez; the core never auto-releases inside instances. Asked by the stock auto release
// through playerbot/ReleaseVeto.h (no override of the stock action name - the 22.08. crash path).
constexpr bool StayDead(bool on, bool inDungeon, bool runActive, bool testParty)
{
    return on && inDungeon && (runActive || testParty);
}

// One loot-yield evaluation (follow-tank, hold-at-camp, the tank's advance). The yield hands the tick to the loot actions
// for at most timeoutMs from its anchor, then the corpse is given up.
//  - partyCombatGate (DungeonClear.LootYieldPartyCombatGate, followers only): no yield while the party fights - the bot
//    keeps to the tank; the anchor is dropped as if there were no loot.
//  - foughtSinceAnchor (DungeonClear.LootYieldReanchorAfterFight): the anchor is set before a fight and the yield is not
//    evaluated in combat, so on the first tick after the fight it was already past the timeout and every follower gave up
//    the fresh corpses at once ("timed out after 50000-116000ms"). A fight that started after the anchor sets it anew.
//    Not a wall-time gap: a bot walking to a corpse is evaluated only every ~3 s (the move action's wait), and a gap rule
//    would re-anchor on every evaluation and never let the timeout end the yield (pre-review 25.10.).
// Both off: exactly the old rule (anchor on the first loot tick, reset when there is no loot).
struct LootYieldStep
{
    std::uint32_t start = 0;   // the anchor to keep (0 = none)
    bool anchored = false;     // set anew in this evaluation (the caller stamps its fight clock)
    bool yield = false;        // hand the tick to the loot actions
    bool giveUp = false;       // past the timeout: give the corpse up and go on
};

inline LootYieldStep EvaluateLootYield(bool loot, bool partyFighting, bool partyCombatGate, std::uint32_t start,
    bool foughtSinceAnchor, std::uint32_t now, std::uint32_t timeoutMs)
{
    LootYieldStep step;
    if (!loot || (partyCombatGate && partyFighting))
        return step;
    if (start == 0 || foughtSinceAnchor)
    {
        start = now;
        step.anchored = true;
    }
    step.start = start;
    if (now - start < timeoutMs)
        step.yield = true;
    else
        step.giveUp = true;
    return step;
}

// The fight clock for foughtSinceAnchor: the bot's last combat start (seconds) strictly after the anchor's second. A fight
// that started in the anchor's own second is not counted - counting it would re-anchor on every evaluation.
constexpr bool FoughtSince(bool on, std::uint32_t anchorSec, std::uint64_t combatStartSec)
{
    return on && anchorSec != 0 && combatStartSec > anchorSec;
}
}
