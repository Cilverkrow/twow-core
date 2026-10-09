#pragma once

#include <cstdint>

// twow-repo#541 (audit A12, AiPlayerbot.Perf.TrainableSpellsPrecheck): pure rules for
// TrainableSpellsValue::Calculate. No engine types and no state.
namespace ai::trainable_spells
{
// The core's skill RED condition in Player::GetTrainerSpellState ("check skill requirement"), word for word:
//   trainer_spell->reqSkill && GetSkillValueBase(trainer_spell->reqSkill) < trainer_spell->reqSkillValue
// Every earlier exit of GetTrainerSpellState returns RED or GRAY and this check returns RED, so a spell
// for which this is true can never come back GREEN. As in the core, skillBase is called only when
// reqSkill != 0. It must return Player::GetSkillValueBase (uint16), so the comparison promotes exactly
// like the core's.
template <class SkillBase>
inline bool SkillRequirementRed(std::uint32_t reqSkill, std::uint32_t reqSkillValue, SkillBase skillBase)
{
    return reqSkill && skillBase(reqSkill) < reqSkillValue;
}

// The roster flag and the profession pair do not depend on the spell.
// reuse off: read at every GREEN spell (old path).
// reuse on: read at the first GREEN spell only (where they are first read today), then reused.
inline bool ShouldReadRosterState(bool reuse, bool alreadyRead)
{
    return !reuse || !alreadyRead;
}
}
