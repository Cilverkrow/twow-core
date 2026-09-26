#pragma once

#include <array>
#include <cstdint>

namespace ai::class_grant
{
// #356 (owner 2026-09-26): roster bots get the rewards of quest-bound class
// abilities at the right level; players keep doing the quests. Quest spells
// already arrive through AutoLearnSpellAction::LearnQuestSpells (all class
// quests with RequiredClasses). The shaman totems slip through it: the water
// totem's final quest (96) carries no class requirement and the air totem
// quest only casts Swift Wind, so the tools are handed out explicitly here.
struct Grant
{
    std::uint32_t level;
    std::uint32_t item;         // totem item
    std::uint32_t teachSpell;   // LEARN_SPELL of the quest reward, 0 = none
};

inline std::array<Grant, 4> const& ShamanTotems()
{
    static std::array<Grant, 4> const grants =
    {{
        {  4, 5175, 8073 },     // Earth Totem, Stoneskin Totem (Call of Earth)
        { 10, 5176, 2075 },     // Fire Totem, Searing Totem (Call of Fire)
        { 20, 5177, 5396 },     // Water Totem, Healing Stream Totem (Call of Water, quest 96)
        { 30, 5178, 0 },        // Air Totem (Call of Air: Swift Wind is a one-off cast)
    }};
    return grants;
}

inline bool IsDue(Grant const& grant, std::uint32_t botLevel)
{
    return botLevel >= grant.level;
}
}
