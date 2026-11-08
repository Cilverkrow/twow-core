#pragma once

#include <atomic>
#include <cstdint>

namespace ai::known_spell_nodes
{
// twow-repo#602 ROT-01 (owner 04.11.2026 "die fähigkeiten ab dem fähigen level"; AiPlayerbot.Rotation.KnownSpellNodes, default
// 1 since owner 08.11.2026 after the A/B, KSN RESULT ADOPT 5beb1182; 0 = rollback). Archived action logs (84 bots, ~4.6 M ticks,
// notes/known-spells RESULT 04.11.2026): 11-51 % of a questing bot's action time
// goes to spell actions that never once return OK - spells not learned yet (cone of cold below 26, mortal strike / mind flay
// talents) and racials of other races (stoneform, war stomp, cannibalize, escape artist) - 0.5-0.8 ms per attempt, every tick,
// even with Rotation.NeverPossibleSkip. With the switch Engine::ProcessTriggers skips a trigger node (no Check(), no push, no
// attempt) when every handler is a spell action whose spell the bot does not know and that has no alternatives. A node with
// alternatives keeps running: its IMPOSSIBLE pushes them (charge -> reach melee). The answer is cached per node and taken again
// when the bot's known-spell stamp changes (learn, level-up, talent points; SpellStampPolicy.h) and at least every RecheckMs,
// so a spell learned now is tried on the next tick. Pure rules, no game types.

// Facts about one handler action of a trigger node, as the engine sees them.
struct Handler
{
    bool spellAction = false;     // a CastSpellAction
    bool keepsUnknown = false;    // runtime spell name / vehicle spell / mount: always keeps the full checks
    bool spellKnown = false;      // the "spell id" value is not 0 (freshly read after a stamp change)
    bool hasAlternatives = false; // the action node (strategy + action) has alternatives
};

// A handler that blocks the skip: anything but an unknown spell action without alternatives.
inline bool Dead(Handler const& h)
{
    return h.spellAction && !h.keepsUnknown && !h.spellKnown && !h.hasAlternatives;
}

constexpr std::uint32_t RecheckMs = 10000;

// Take the node's answer again: never taken, the stamp changed, or older than RecheckMs (a stale "spell id" value after a learn).
inline bool Recheck(bool taken, std::uint64_t cachedStamp, std::uint64_t stamp, std::uint32_t nowMs, std::uint32_t takenAtMs)
{
    return !taken || cachedStamp != stamp || std::uint32_t(nowMs - takenAtMs) >= RecheckMs;
}

// Per-minute counts for the [KnownSpellNodes] line (with the switch on), per class id 0..MaxClass-1; increments.
constexpr std::uint32_t MaxClass = 12;
struct Counters
{
    std::atomic<std::uint64_t> skipped[MaxClass] {};   // trigger checks skipped
    std::atomic<std::uint64_t> checked[MaxClass] {};   // trigger checks run by bots the switch applies to
    std::atomic<std::uint64_t> recomputes {0};         // node answers taken again
    std::atomic<std::uint64_t> stampChanges {0};       // of those, after a stamp change
};

inline Counters& Global()
{
    static Counters counters;
    return counters;
}
}
