#include "playerbot/playerbot.h"
#include "RestGateValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/RestGateTrace.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

namespace
{
// twow-repo#602 X1 (E1): solo free bots only. Groups mix both cohorts (cohort = guid parity), so a resting B member would hold up A.
bool RestBot(PlayerbotAI* ai, Player* bot)
{
    return !bot->GetGroup() && !ai->IsRealPlayer() && !ai->HasRealPlayerMaster() && sRandomPlayerbotMgr.IsFreeBot(bot) &&
        !bot->InBattleGround();
}

ai::rest_gate::Thresholds ConfiguredThresholds()
{
    ai::rest_gate::Thresholds t;
    t.startHp = sPlayerbotAIConfig.rotationRestGateStartHp;
    t.startMana = sPlayerbotAIConfig.rotationRestGateStartMana;
    t.targetHp = sPlayerbotAIConfig.rotationRestGateTargetHp;
    t.targetMana = sPlayerbotAIConfig.rotationRestGateTargetMana;
    t.maxRestSeconds = sPlayerbotAIConfig.rotationRestGateMaxRestSeconds;
    return t;
}

ai::rest_gate::Reading Read(Player* bot)
{
    ai::rest_gate::Reading r;
    r.hp = uint32(bot->GetHealthPercent());
    r.manaPool = bot->HasMana() && bot->GetMaxPower(POWER_MANA) > 0;
    r.mana = r.manaPool ? uint32(bot->GetPowerPercent(POWER_MANA)) : 100;
    r.inCombat = bot->IsInCombat();
    r.attacked = !bot->getAttackers().empty();
    return r;
}

RestGateValue* ValueOf(PlayerbotAI* ai)
{
    return dynamic_cast<RestGateValue*>(ai->GetAiObjectContext()->GetUntypedValue("rest gate"));
}
}

bool RestGateValue::Resting(PlayerbotAI* ai)
{
    return sPlayerbotAIConfig.rotationRestGate && ai->GetAiObjectContext()->GetValue<bool>("rest gate")->Get();
}

void RestGateValue::NoteCastFail(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    ai::rest_gate_rt::RecordCastFail(bot->getClass(), ai::cohort::InB(bot->GetGUIDLow()));
}

void RestGateValue::OnCombatStarted(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.rotationRestGate)
        return;
    if (RestGateValue* value = ValueOf(ai))
        value->CloseOnEvent(ai::rest_gate::End::Combat);
}

void RestGateValue::OnDeath(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.rotationRestGate)
        return;
    if (RestGateValue* value = ValueOf(ai))
        value->CloseOnEvent(ai::rest_gate::End::Death);
}

void RestGateValue::CloseOnEvent(ai::rest_gate::End end)
{
    if (!episode.open)
        return;
    Finish(ai::rest_gate::Close(episode, end, WorldTimer::getMSTime()), Read(bot));
    Reset();   // the next read recalculates instead of the cached "resting"
}

// One finished episode: counters, the budgeted line, and (applied only) the travel target gets its pause (OB-10 condition 1).
void RestGateValue::Finish(ai::rest_gate::Closed const& closed, ai::rest_gate::Reading const& r)
{
    if (!closed.closed)
        return;
    uint32 const guid = bot->GetGUIDLow();
    ai::rest_gate_rt::RecordEpisode(bot->getClass(), ai::cohort::InB(guid), closed.end, closed.durMs);
    if (ai::rest_gate_rt::TakeLineBudget(uint32(time(nullptr))))
        sLog.outBasic("%s", ai::rest_gate::FormatEpisodeLine(guid, bot->GetName(), bot->getClass(), bot->GetLevel(), ai::cohort::Letter(guid),
            closed.applied, closed.startHp, closed.startMana, r.hp, r.manaPool ? int(r.mana) : -1, closed.durMs, closed.end,
            !ai->AllowActivity()).c_str());
    if (closed.applied)
        if (TravelTarget* target = AI_VALUE(TravelTarget*, "travel target"))
            target->OnRestEnded(closed.durMs);
}

bool RestGateValue::Calculate()
{
    uint32 const mode = sPlayerbotAIConfig.rotationRestGate;
    // Switch off, out of the population (grouped, battleground) or dead: the episode is dropped without a line.
    if (!mode || !bot->IsAlive() || !RestBot(ai, bot))
    {
        episode.open = false;
        return false;
    }

    ai::rest_gate::Reading const r = Read(bot);
    ai::rest_gate::Closed const closed = ai::rest_gate::Step(episode, r, ConfiguredThresholds(), WorldTimer::getMSTime(),
        ai::cohort::Applies(mode, bot->GetGUIDLow()));
    Finish(closed, r);
    return ai::rest_gate::Resting(episode);
}
