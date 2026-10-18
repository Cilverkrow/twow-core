#include "playerbot/playerbot.h"
#include "StationCraftTrigger.h"
#include "ProfessionUseTriggers.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/ProfessionUsePolicy.h"
#include "playerbot/DirectTargetDestinations.h"

#include <vector>

using namespace ai;

bool StationCraftTrigger::IsActive()
{
    // twow-repo#594 / P3-b: built at boot and switched on, ProfessionUse.Craft and RealReagents, a roster bot on its own.
    if (!direct_targets::StationCraftLive() || !sPlayerbotAIConfig.professionUseCraft ||
        !sPlayerbotAIConfig.professionUseRealReagents || !IsRosterBotOnItsOwn(ai))
        return false;

    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    if (!target || target->GetStatus() != TravelStatus::TRAVEL_STATUS_WORK || !direct_targets::TagOf(target->GetDestination()))
        return false;

    StationTravelDestination* station = dynamic_cast<StationTravelDestination*>(target->GetDestination());
    if (!station || !direct_targets::IsLive(station->GetKind()))
        return false;

    time_t const now = time(nullptr);

    // The last station pick, once the "profession craft" action used it up: a backoff set for it since the pick with
    // tools and reagents still in the bags is no_focus (the station is not in reach) - the trip ends. Otherwise the
    // pick counts as a craft.
    int32 const picked = AI_VALUE2(int, "manual int", "station craft pick");
    if (picked > 0)
    {
        if (AI_VALUE2(int, "manual int", "profession craft spell") == picked)
            return IsReadyToCraft(ai);

        SET_AI_VALUE2(int, "manual int", "station craft pick", 0);
        time_t const pickedAt = time_t(std::max(0, AI_VALUE2(int, "manual int", "station craft pick at")));
        bool const failed = AI_VALUE2(int, "manual int", "profession craft failed spell") == picked &&
            AI_VALUE2(time_t, "manual time", "profession craft failed until") > pickedAt;
        if (!failed)
            direct_targets::OnStationCraft(ai, station);
        else
        {
            SpellEntry const* spell = sServerFacade.LookupSpellInfo(uint32(picked));
            if (spell && HasCraftTools(spell, bot) && CraftableFromBags(spell, bot) > 0)
            {
                direct_targets::Stats().noFocus.fetch_add(1, std::memory_order_relaxed);
                direct_targets::OnTravelFailure(ai, station, "no_focus");
                target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                return false;
            }
        }
    }

    if (!IsReadyToCraft(ai) || AI_VALUE(uint8, "bag space") > 80)
        return false;

    // A pick of the normal trigger still waits for the action.
    if (AI_VALUE2(int, "manual int", "profession craft spell") > 0)
        return false;

    std::vector<profession_use::Recipe> recipes;
    for (uint32 spellId : AI_VALUE(std::vector<uint32>, "craft spells"))
    {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
        if (!spell || spell->RequiresSpellFocus != station->GetFocusId())
            continue;
        if (!direct_targets::StationRecipeUsable(ai, spell, now))
            continue;

        // The focus is in reach here, so the recipe ranks like one without focus (lowest skill first, the profession
        // crafted last time loses a tie, then the skill-up chance and the casts - hotfix 8.20).
        profession_use::Recipe recipe;
        recipe.spellId = spellId;
        recipe.givesSkillUp = true;
        recipe.hasReagents = true;
        recipe.hasTools = true;
        recipe.craftable = CraftableFromBags(spell, bot);
        recipe.skillUpChance = RecipeSkillUpChance(spellId, bot, &recipe.skillId, &recipe.skillValue);
        if (!recipe.skillId)
        {
            SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
            for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second && !recipe.skillId; ++itr)
                recipe.skillId = itr->second->skillId;
            recipe.skillValue = recipe.skillId ? bot->GetSkillValuePure(recipe.skillId) : 0;
        }
        recipes.push_back(recipe);
    }

    uint32 const lastSkill = uint32(std::max(0, AI_VALUE2(int, "manual int", "profession craft last skill")));
    int const pick = profession_use::Pick(recipes, lastSkill);
    if (pick < 0)
        return false;

    uint32 const spellId = recipes[std::size_t(pick)].spellId;
    SET_AI_VALUE2(int, "manual int", "profession craft spell", int32(spellId));
    SET_AI_VALUE2(int, "manual int", "profession craft last skill", int32(recipes[std::size_t(pick)].skillId));
    SET_AI_VALUE2(int, "manual int", "station craft pick", int32(spellId));
    SET_AI_VALUE2(int, "manual int", "station craft pick at", int32(now));
    return true;
}
