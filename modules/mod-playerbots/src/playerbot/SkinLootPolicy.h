#pragma once

#include <cstdint>

namespace ai::skin_loot
{
// twow-repo#485 (#471): roster skinners hardly skinned (v23: 3 of 133 corpses they could
// skin, v24: 0 of 37). The core refuses to skin while loot is left on the corpse
// (TARGET_NOT_LOOTED), and bots leave items worth less than 1/1000 of their money on it.

// Skill a corpse needs before the core lets the skinner cast. Mirrors Spell::CheckCast,
// SPELL_EFFECT_SKINNING (src/game/Spells/Spell.cpp); skin_loot_source_contract pins both
// lines, so a change in the core fails the contract instead of drifting silently.
inline int32_t RequiredSkinningSkill(int32_t creatureLevel, int32_t skill)
{
    return skill < 100 ? (creatureLevel - 10) * 10 : creatureLevel * 5;
}

// A roster bot on its own that can skin this corpse right now (skill, skinning knife)
// takes every item from it, junk included, so the corpse becomes skinnable.
inline bool ShouldClearCorpseForSkinning(bool enabled, bool rosterOnItsOwn, bool corpseLoot, bool skinnable,
    bool hasSkinning, bool hasKnife, int32_t skill, int32_t creatureLevel)
{
    return enabled && rosterOnItsOwn && corpseLoot && skinnable && hasSkinning && hasKnife &&
        RequiredSkinningSkill(creatureLevel, skill) <= skill;
}

// A corpse with loot left on it is never a skinning target, whoever the loot belongs to:
// the core refuses the cast until the loot is gone.
inline bool IsSkinTarget(bool lootable, bool skinnable)
{
    return !lootable && skinnable;
}

// The [ProfessionUse] stage=skin line after a corpse was cleared for skinning.
// reason=junk_taken only when the switch made a difference: at least one stored item
// IsLootAllowed refuses (junk); detail is that count. A corpse emptied without junk (every
// item allowed anyway, or money only) is reason=nothing_blocked; loot left on it (bags, loot
// rights) is state=skipped reason=loot_left. Both of these give the items taken as detail.
struct ClearTrace
{
    char const* state;
    char const* reason;
    uint32_t detail;
};

inline ClearTrace TraceAfterClear(bool corpseLooted, uint32_t itemsTaken, uint32_t junkTaken)
{
    if (!corpseLooted)
        return { "skipped", "loot_left", itemsTaken };
    if (junkTaken > 0)
        return { "cleared", "junk_taken", junkTaken };
    return { "cleared", "nothing_blocked", itemsTaken };
}
}
