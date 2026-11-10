#include "playerbot/playerbot.h"
#include "ProfessionUseActions.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/triggers/ProfessionUseTriggers.h"
#include "playerbot/CohortPolicy.h"

#include <algorithm>
#include <string>

using namespace ai;

namespace
{
    // twow-repo#485: only a lasting refusal from Spell::CheckItems blocks the
    // recipe for CraftFailBackoffSeconds: a missing reagent, tool or spell
    // focus. Any other refusal (global cooldown, loss of control) is transient
    // and retried after the craft interval; the trigger skips recipes on
    // cooldown and recipes whose product the bags cannot take.
    char const* BackoffReason(SpellCastResult result)
    {
        switch (result)
        {
            case SPELL_FAILED_ITEM_NOT_READY:
            case SPELL_FAILED_REAGENTS:
                return "no_reagents";
            case SPELL_FAILED_ITEM_GONE:
            case SPELL_FAILED_TOTEMS:
                return "no_tool";
            case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
                return "no_focus";
            default:
                return nullptr;
        }
    }

    // DONT_REPORT is transient as well: Spell::CheckCast returns it before
    // CheckItems at an ended battleground (STATUS_WAIT_LEAVE) or while the bot
    // is banished or about to be stunned, which CanCastSpell does not catch.
    // The room for the product (CheckItems: unique item carried, full bags)
    // is checked by the trigger before the pick. Any other refusal carries its
    // cast result like hotfix 8.13's self-cast failures (logged, never said),
    // under its own prefix: 8.13 traces "rpg craft" failures as cast_result_<n>
    // in the same [ProfessionUse] craft failed line, and the paths stay apart.
    std::string TransientReason(SpellCastResult result)
    {
        if (result == SPELL_FAILED_DONT_REPORT)
            return "dont_report";
        return "precheck_result_" + std::to_string(uint32(result));
    }
}

bool ProfessionCraftAction::Execute(Event& event)
{
    // twow-repo#333 P2-a: each run starts from the react delay, so a cast time never carries over to the next run.
    SetDuration(sPlayerbotAIConfig.reactDelay);
    SET_AI_VALUE2(time_t, "manual time", "profession craft", time(nullptr));

    if (sPlayerbotAIConfig.professionUseRealReagents)
    {
        // twow-repo#485: cast the recipe the trigger picked from the bags, with
        // the bot itself as explicit target: no "castnc" whisper whose basket
        // expires after 5 s, no selected NPC or mob as target (BAD_TARGETS,
        // TARGET_ENEMY). The pick is used up here.
        uint32 const spellId = uint32(std::max(0, AI_VALUE2(int, "manual int", "profession craft spell")));
        SET_AI_VALUE2(int, "manual int", "profession craft spell", 0);
        SpellEntry const* spell = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
        if (!spell)
            return false;

        // Trade skills cannot be cast in cat, bear, travel or aquatic form or as
        // ghost wolf (SPELL_ATTR_NOT_SHAPESHIFT); drop the form first, like the
        // legacy "castnc" path does.
        if (bot->GetShapeshiftForm() != FORM_NONE)
            ai->RemoveShapeshift();

        // twow-repo#333 M2 Crafting.Cooking (owner list 07.11.2026, item 2): a fire recipe without a fire in reach - light the
        // campfire first and keep the pick, so the trigger fires again right after the fire instead of after the craft interval.
        // A fire that cannot be lit leaves the recipe to the cast check below (no_focus backoff).
        bool const cooking = spell->RequiresSpellFocus == kCookingFireFocus && CraftingCookingFor(ai);
        if (cooking && !CookingFireNear(bot) && CanLightCampfire(bot))
        {
            SpellCastResult fireCheck = SPELL_CAST_OK;
            uint32 fireDuration = 0;
            bool const lit = ai->CanCastSpell(kCampfireSpell, bot, 0, true, nullptr, false, false, false, &fireCheck) &&
                ai->CastSpell(kCampfireSpell, bot, nullptr, true, &fireDuration);
            sLog.outBasic("[Crafting] kind=cooking event=campfire result=%s check=%u recipe=%u bot=%u level=%u cohort=%c",
                lit ? "cast_started" : "failed", uint32(fireCheck), spellId, bot->GetGUIDLow(), bot->GetLevel(),
                ai::cohort::Letter(bot->GetGUIDLow()));
            if (lit)
            {
                time_t const now = time(nullptr);
                SetDuration(std::max(fireDuration, sPlayerbotAIConfig.reactDelay));
                SET_AI_VALUE2(int, "manual int", "profession craft spell", int32(spellId));
                SET_AI_VALUE2(time_t, "manual time", "profession craft scan", now);
                SET_AI_VALUE2(time_t, "manual time", "profession craft", now - time_t(sPlayerbotAIConfig.professionUseCraftIntervalSeconds));
                return true;
            }
        }

        SpellCastResult check = SPELL_CAST_OK;
        if (!ai->CanCastSpell(spellId, bot, 0, true, nullptr, false, false, false, &check))
        {
            char const* backoff = BackoffReason(check);
            if (backoff)
            {
                SET_AI_VALUE2(int, "manual int", "profession craft failed spell", int32(spellId));
                SET_AI_VALUE2(time_t, "manual time", "profession craft failed until",
                    time(nullptr) + time_t(sPlayerbotAIConfig.professionUseCraftFailBackoffSeconds));
            }
            std::string const reason = backoff ? std::string(backoff) : TransientReason(check);
            TraceProfessionUse(ai, "craft", "failed", reason.c_str(), spellId);
            return false;
        }

        // Hotfix 8.13 alignment: this direct cast never passes CastCustomSpellAction,
        // so one cast gets exactly one line. "cast_started" only after
        // Spell::SpellStart returned SPELL_CAST_OK (PlayerbotAI::CastSpell);
        // a failure is only logged, the bot says nothing (no requester).
        // twow-repo#333 P2-a: hold the AI for the recipe's cast time, as every other cast action does. The engine sets
        // the AI delay to this action's duration after it ran; with the react delay the next movement action
        // interrupted the craft (M1r run 4: 29 of 234 bandage / cloth casts INTERRUPTED or SPELL_IN_PROGRESS).
        uint32 castDuration = 0;
        bool const ok = ai->CastSpell(spellId, bot, nullptr, true, &castDuration);
        TraceProfessionUse(ai, "craft", ok ? "cast_started" : "failed", ok ? "real_reagents" : "cast_failed", spellId);
        if (ok)
        {
            SetDuration(std::max(castDuration, sPlayerbotAIConfig.reactDelay));
            sPlayerbotAIConfig.logEvent(ai, "CraftCastStarted", spell->SpellName[0], std::to_string(spellId));
            // twow-repo#333 T28-M (AiPlayerbot.Gathering.Trace): a started craft cast and its cast time.
            if (sPlayerbotAIConfig.gatheringTrace)
                GatheringTraceCraft(ai, castDuration);
            // twow-repo#333 M2 Crafting.Cooking: one line per started fire recipe of the switch's bots.
            if (cooking)
                sLog.outBasic("[Crafting] kind=cooking event=cook spell=%u item=%u bot=%u level=%u cohort=%c", spellId,
                    uint32(spell->EffectItemType[0]), bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()));
            // twow-repo#333 M2 Crafting.Survival (owner list 07.11.2026, item 3): one line per started survival recipe (the skill
            // line the trigger picked it for) of the switch's bots.
            if (AI_VALUE2(int, "manual int", "profession craft last skill") == int(SKILL_SURVIVAL2) && CraftingSurvivalFor(ai))
                sLog.outBasic("[Crafting] kind=survival event=craft spell=%u item=%u skill=%u bot=%u level=%u cohort=%c", spellId,
                    uint32(spell->EffectItemType[0]), uint32(bot->GetSkillValuePure(SKILL_SURVIVAL2)), bot->GetGUIDLow(), bot->GetLevel(),
                    ai::cohort::Letter(bot->GetGUIDLow()));
        }
        return ok;
    }

    // Hotfix 8.13: "craft random item" only sends the cast command; "started" is traced by
    // CastCustomSpellAction once the cast really began, a failure there with its cast result.
    bool const requested = ai->DoSpecificAction("craft random item", Event(), true);
    if (!requested)
        TraceProfessionUse(ai, "craft", "failed", "cast_not_started", uint32(bot->GetLevel()));
    return requested;
}

bool ProfessionCraftAction::isUseful()
{
    // twow-repo#485: the queued action may run after the bot started moving.
    return !sPlayerbotAIConfig.professionUseRealReagents || IsReadyToCraft(ai);
}
