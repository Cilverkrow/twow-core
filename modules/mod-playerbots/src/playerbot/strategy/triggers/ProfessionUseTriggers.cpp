#include "playerbot/playerbot.h"
#include "ProfessionUseTriggers.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/ProfessionUsePolicy.h"
#include "playerbot/strategy/values/CraftValues.h"

using namespace ai;

bool ai::IsRosterBotOnItsOwn(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return bot && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
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

    std::vector<profession_use::Recipe> recipes;
    for (uint32 spellId : AI_VALUE(std::vector<uint32>, "craft spells"))
    {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
        if (!spell)
            continue;

        profession_use::Recipe recipe;
        recipe.needsFocus = spell->RequiresSpellFocus != 0;
        recipe.givesSkillUp = !recipe.needsFocus && ShouldCraftSpellValue::SpellGivesSkillUp(spellId, bot);
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

    return true;
}
