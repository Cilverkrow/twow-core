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
}
