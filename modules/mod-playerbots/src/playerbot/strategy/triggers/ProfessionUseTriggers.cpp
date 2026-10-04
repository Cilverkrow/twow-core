#include "playerbot/playerbot.h"
#include "ProfessionUseTriggers.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/ProfessionUsePolicy.h"
#include "playerbot/strategy/values/CraftValues.h"

#include <algorithm>
#include <limits>

using namespace ai;

namespace
{
    // twow-repo#485: skill-up chance in per mille, rolled the way
    // Player::UpdateCraftSkill does (skill_line_ability min/max values,
    // SkillChance.*); a skill at its rank cap is skipped like in
    // ShouldCraftSpellValue::SpellGivesSkillUp.
    uint32 CraftSkillUpChance(uint32 spellId, Player* bot, uint32* outSkillId = nullptr, uint32* outSkillValue = nullptr)
    {
        SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
        for (SkillLineAbilityMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* skill = itr->second;
            if (!skill->skillId)
                continue;

            uint32 const skillValue = bot->GetSkillValuePure(skill->skillId);
            if (bot->GetSkillMaxPure(skill->skillId) <= skillValue)
                continue;

            uint32 chance = sWorld.getConfig(CONFIG_UINT32_SKILL_CHANCE_ORANGE);
            if (skillValue >= skill->max_value)
                chance = sWorld.getConfig(CONFIG_UINT32_SKILL_CHANCE_GREY);
            else if (skillValue >= (skill->max_value + skill->min_value) / 2)
                chance = sWorld.getConfig(CONFIG_UINT32_SKILL_CHANCE_GREEN);
            else if (skillValue >= skill->min_value)
                chance = sWorld.getConfig(CONFIG_UINT32_SKILL_CHANCE_YELLOW);

            if (chance)
            {
                if (outSkillId)
                    *outSkillId = skill->skillId;
                if (outSkillValue)
                    *outSkillValue = skillValue;
                return chance * 10;
            }
        }
        return 0;
    }
}

// Every tool of the recipe is in the bags, as Spell::CheckItems demands
// (else SPELL_FAILED_ITEM_GONE).
bool ai::HasCraftTools(SpellEntry const* spell, Player* bot)
{
    for (uint8 i = 0; i < MAX_SPELL_TOTEMS; ++i)
        if (spell->Totem[i] && !bot->HasItemCount(spell->Totem[i], 1))
            return false;
    return true;
}

// Casts the reagents in the bags pay for (Spell::CheckItems, else
// SPELL_FAILED_ITEM_NOT_READY). Unlike "has reagents for" this ignores the
// item cheat. A recipe without reagents is not limited by them.
uint32 ai::CraftableFromBags(SpellEntry const* spell, Player* bot)
{
    uint32 craftable = std::numeric_limits<uint32>::max();
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spell->Reagent[i] <= 0 || !spell->ReagentCount[i])
            continue;
        craftable = std::min(craftable, bot->GetItemCount(uint32(spell->Reagent[i])) / spell->ReagentCount[i]);
    }
    return craftable;
}

namespace
{
    // The bags take one product, as Spell::CheckItems demands for a craft cast
    // (else SPELL_FAILED_DONT_REPORT): no unique item already carried, room
    // left. Such a recipe is skipped without a backoff until they can.
    bool HasRoomForProduct(SpellEntry const* spell, Player* bot)
    {
        ItemPosCountVec dest;
        return bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, spell->EffectItemType[0], 1) == EQUIP_ERR_OK;
    }
}

bool ai::IsRosterBotOnItsOwn(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return bot && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
}

bool ai::IsReadyToCraft(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return profession_use::IsIdleForCraft(sServerFacade.isMoving(bot) || bot->IsTaxiFlying(), sServerFacade.IsInCombat(bot),
        bot->IsNonMeleeSpellCasted(true), !bot->IsStandState(), bot->IsMounted());
}

void ai::TraceProfessionUse(PlayerbotAI* ai, char const* stage, char const* state, char const* reason, uint32 detail)
{
    if (!sPlayerbotAIConfig.professionUseTrace || !IsRosterBotOnItsOwn(ai))
        return;

    // One line per bot, stage, state and reason per cooldown: bounded even
    // though the gather check runs every second over every visible node.
    AiObjectContext* context = ai->GetAiObjectContext();
    std::string const key = std::string("profession use ") + stage + " " + state + " " + reason;
    time_t const now = time(nullptr);
    if (!profession_use::IsDue(now, AI_VALUE2(time_t, "manual time", key), sPlayerbotAIConfig.professionUseTraceCooldownSeconds))
        return;
    SET_AI_VALUE2(time_t, "manual time", key, now);

    Player* bot = ai->GetBot();
    sLog.outBasic("[ProfessionUse] stage=%s state=%s reason=%s bot=%u level=%u detail=%u",
        stage, state, reason, bot->GetGUIDLow(), bot->GetLevel(), detail);
}

bool ProfessionCraftTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.professionUseCraft || !IsRosterBotOnItsOwn(ai))
        return false;

    time_t const now = time(nullptr);
    if (!profession_use::IsDue(now, AI_VALUE2(time_t, "manual time", "profession craft"), sPlayerbotAIConfig.professionUseCraftIntervalSeconds))
        return false;

    if (AI_VALUE(uint8, "bag space") > 80)
        return false;

    // twow-repo#485 (RealReagents): a cast now would fail (and stop a moving
    // bot), so wait without using up the interval. A pick that still waits for
    // the action is reused for a minute instead of a full scan every 10 s.
    bool const realReagents = sPlayerbotAIConfig.professionUseRealReagents;
    if (realReagents)
    {
        if (!IsReadyToCraft(ai))
            return false;

        if (AI_VALUE2(int, "manual int", "profession craft spell") > 0 &&
            !profession_use::IsDue(now, AI_VALUE2(time_t, "manual time", "profession craft scan"), 60))
            return true;
    }

    int32 const failedSpell = realReagents ? AI_VALUE2(int, "manual int", "profession craft failed spell") : 0;
    bool const failedBackoff = realReagents && now < AI_VALUE2(time_t, "manual time", "profession craft failed until");

    std::vector<profession_use::Recipe> recipes;
    for (uint32 spellId : AI_VALUE(std::vector<uint32>, "craft spells"))
    {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
        if (!spell)
            continue;

        profession_use::Recipe recipe;
        recipe.needsFocus = spell->RequiresSpellFocus != 0;
        recipe.givesSkillUp = !recipe.needsFocus && ShouldCraftSpellValue::SpellGivesSkillUp(spellId, bot);
        if (realReagents)
        {
            // Reagents and tools from the bags: under the item cheat "can craft
            // spell" is true for every recipe.
            recipe.spellId = spellId;
            if (recipe.givesSkillUp)
            {
                recipe.skillUpChance = CraftSkillUpChance(spellId, bot, &recipe.skillId, &recipe.skillValue);
                recipe.hasTools = HasCraftTools(spell, bot);
                recipe.craftable = CraftableFromBags(spell, bot);
                // A recipe on its own or category cooldown (transmutes: 24-48 h)
                // fails with NOT_READY like Spell::CheckCast and, ranked first,
                // would block every other recipe until it is ready again. So
                // would one whose product the bags cannot take (DONT_REPORT,
                // which backs nothing off): a unique item already carried.
                recipe.backedOff = (failedBackoff && failedSpell == int32(spellId)) || bot->HasSpellCooldown(spellId) ||
                    (spell->Category && bot->HasSpellCategoryCooldown(spell->Category)) ||
                    (recipe.hasTools && recipe.craftable > 0 && !HasRoomForProduct(spell, bot));
            }
            recipe.hasReagents = recipe.givesSkillUp && recipe.hasTools && recipe.craftable > 0;
        }
        else
            recipe.hasReagents = recipe.givesSkillUp && AI_VALUE2(bool, "can craft spell", spellId);
        recipes.push_back(recipe);
    }

    profession_use::CraftBlock const block = profession_use::Classify(recipes);
    if (block != profession_use::CraftBlock::None)
    {
        // Nothing to craft now: wait a full interval before looking again.
        SET_AI_VALUE2(time_t, "manual time", "profession craft", now);
        TraceProfessionUse(ai, "craft", "skipped", profession_use::Name(block), uint32(recipes.size()));
        return false;
    }

    if (realReagents)
    {
        // Classify does not know the backoff: when every craftable recipe is
        // backed off, on cooldown or without room for its product there is no
        // pick, so wait a full interval.
        // Hotfix 8.20: lowest profession first, the profession crafted last time loses a tie.
        uint32 const lastSkill = uint32(std::max(0, AI_VALUE2(int, "manual int", "profession craft last skill")));
        int const pick = profession_use::Pick(recipes, lastSkill);
        if (pick < 0)
        {
            SET_AI_VALUE2(time_t, "manual time", "profession craft", now);
            TraceProfessionUse(ai, "craft", "skipped", "backed_off", 0);
            return false;
        }

        SET_AI_VALUE2(int, "manual int", "profession craft spell", int32(recipes[std::size_t(pick)].spellId));
        SET_AI_VALUE2(int, "manual int", "profession craft last skill", int32(recipes[std::size_t(pick)].skillId));
        SET_AI_VALUE2(time_t, "manual time", "profession craft scan", now);
    }

    return true;
}
