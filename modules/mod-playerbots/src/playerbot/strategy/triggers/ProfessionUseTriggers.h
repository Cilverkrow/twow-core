#pragma once

#include "playerbot/strategy/Trigger.h"

namespace ai
{
    // #333: throttled [ProfessionUse] diagnostics for roster bots (BASIC level,
    // AiPlayerbot.ProfessionUse.Trace).
    void TraceProfessionUse(PlayerbotAI* ai, char const* stage, char const* state, char const* reason, uint32 detail);

    bool IsRosterBotOnItsOwn(PlayerbotAI* ai);

    // twow-repo#333 M2 Crafting.Consumables (owner list 07.11.2026, item 1): the switch is on for this bot - a roster bot on its own
    // in the switch's cohort (CohortPolicy.h). The config check first, so the default costs nothing.
    bool CraftingConsumablesFor(PlayerbotAI* ai);

    // twow-repo#333 M2 Crafting.Cooking (owner list 07.11.2026, item 2): the switch is on for this bot - a roster bot on its own
    // in the switch's cohort (CohortPolicy.h). The config check first, so the default costs nothing.
    bool CraftingCookingFor(PlayerbotAI* ai);
    // twow-repo#333 M2 Crafting.Survival (owner list 07.11.2026, item 3): the same for its switch.
    bool CraftingSurvivalFor(PlayerbotAI* ai);

    // The cooking fire: spell focus 4 (Basic Campfire GO 29784, any fire counts as in Spell::CheckCast), lit by Basic Campfire 818
    // (learned with cooking; reagent Simple Wood 4470, tool Flint and Tinder 4471, category cooldown).
    constexpr uint32 kCookingFireFocus = 4;
    constexpr uint32 kCampfireSpell = 818;
    // A spell focus kCookingFireFocus within Spell::CheckCast's reach (GameObjectFocusCheck, 10 yd).
    bool CookingFireNear(Player* bot);
    // The bot can light a campfire now: knows 818, not on (category) cooldown, wood and flint in the bags.
    bool CanLightCampfire(Player* bot);

    // twow-repo#485: not moving (nor on a flight path), fighting, casting,
    // sitting or mounted - a craft cast would fail now.
    bool IsReadyToCraft(PlayerbotAI* ai);

    // twow-repo#485 / hotfix 8.16a: reagents and tools from the bags, regardless
    // of the item cheat (also used by "craft random item", the rpg craft path).
    bool HasCraftTools(SpellEntry const* spell, Player* bot);
    uint32 CraftableFromBags(SpellEntry const* spell, Player* bot);

    // twow-repo#594 / P3-b: the skill-up chance (per mille) the "profession craft" trigger ranks its recipes by, for the
    // focus recipes of the station craft trigger; skillId / skillValue of the skill line it rolls for.
    uint32 RecipeSkillUpChance(uint32 spellId, Player* bot, uint32* outSkillId, uint32* outSkillValue);

    // #333: minimal crafting loop. Active for a roster bot on its own when a
    // recipe without spell focus still gives a skill-up and its reagents are in
    // the bags, at most once per AiPlayerbot.ProfessionUse.CraftIntervalSeconds.
    class ProfessionCraftTrigger : public Trigger
    {
    public:
        ProfessionCraftTrigger(PlayerbotAI* ai) : Trigger(ai, "profession craft", 10) {}
        bool IsActive() override;
    };
}
