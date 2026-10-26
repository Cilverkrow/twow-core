#pragma once

// twow-repo#601 cr2-C GO diagnosis (CLI-Deep 26.10.2026, AiPlayerbot.QuestFirstProgression.SkipSkillLockedObjectives; rules agreed
// with CLI-Berufe): Copper Vein (lock 38 = Mining) is a source of Blood Shard 5075 for quests 889 / 5052, and the travel graph sent
// bots without Mining there as a quest objective - 19 arrivals, 18 work timeouts, 0 credit, 6 QuestLoop suppressions in cohort B.
// A lock opens when ANY of its slots fits (Spell::CanOpenLock): a slot without a profession skill (open, kneeling, a key item) counts
// as openable for every bot - lock 259 of the quest herbs has Herbalism plus "open kneeling" and stays a target for all; a
// profession slot only with the skill, a value >= max(1, the lock's value) and the gathering tool (Mining: the pick). Skinning
// concerns creatures only, never a game object lock.

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ai::quest_skill_lock
{
    struct Slot
    {
        bool profession = false;   // a skill slot whose lock type maps to a profession skill (SkillByLockType != SKILL_NONE)
        bool met = false;          // only read for a profession slot
    };

    inline bool ProfessionSlotMet(bool hasSkill, std::uint32_t value, std::uint32_t required, bool hasTool)
    {
        return hasSkill && hasTool && value >= std::max<std::uint32_t>(1, required);
    }

    // True = this bot can open the lock. No slot at all (a lock without requirements): openable.
    inline bool Openable(Slot const* slots, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (!slots[i].profession || slots[i].met)
                return true;
        return count == 0;
    }
}
