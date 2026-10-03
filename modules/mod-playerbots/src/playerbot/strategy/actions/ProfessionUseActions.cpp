#include "playerbot/playerbot.h"
#include "ProfessionUseActions.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/triggers/ProfessionUseTriggers.h"

#include <algorithm>

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
    // is checked by the trigger before the pick.
    char const* TransientReason(SpellCastResult result)
    {
        return result == SPELL_FAILED_DONT_REPORT ? "dont_report" : "not_castable";
    }
}

bool ProfessionCraftAction::Execute(Event& event)
{
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
            TraceProfessionUse(ai, "craft", "failed", backoff ? backoff : TransientReason(check), spellId);
            return false;
        }

        bool const ok = ai->CastSpell(spellId, bot);
        TraceProfessionUse(ai, "craft", ok ? "cast_started" : "failed", ok ? "real_reagents" : "cast_failed", spellId);
        if (ok)
            sPlayerbotAIConfig.logEvent(ai, "CraftCastStarted", spell->SpellName[0], std::to_string(spellId));
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
