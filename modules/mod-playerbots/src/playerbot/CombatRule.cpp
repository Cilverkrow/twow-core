#include "playerbot/playerbot.h"
#include "playerbot/CombatRule.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/strategy/Strategy.h"

#include <ctime>
#include <string>

namespace ai::combat_rule
{
bool HealerMayAttack(PlayerbotAI* ai)
{
    // The old rule's facts; offdps is read only for a healer, as the old expression's short circuit did.
    bool const heal = ai->ContainsStrategy(STRATEGY_TYPE_HEAL);
    bool const offdps = heal && ai->HasStrategy("offdps", BotState::BOT_STATE_COMBAT);
    uint32 const mode = sPlayerbotAIConfig.combatRuleHealerStart;
    // Part off, or the old rule already lets the bot attack: the old expression, nothing else is read.
    if (!mode || !heal || offdps)
        return HealerMayAttack(heal, offdps, false, false);

    Player* const bot = ai->GetBot();
    bool const candidate = HealerWithoutOffdpsCreator(bot->getClass(), ai->HasStrategy("holy", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("restoration", BotState::BOT_STATE_COMBAT));
    if (!candidate)
        return HealerMayAttack(heal, offdps, false, false);

    uint32 const guidLow = bot->GetGUIDLow();
    bool const applies = ai::cohort::Applies(mode, guidLow);
    HealerStartCounters& c = HealerStartGlobal();
    unsigned const cohort = ai::cohort::InB(guidLow) ? 1u : 0u;
    c.asked[cohort].fetch_add(1, std::memory_order_relaxed);
    bool const lifted = HealerMayAttack(heal, offdps, applies, candidate);
    if (lifted)
    {
        c.lifted[cohort].fetch_add(1, std::memory_order_relaxed);
        if (bot->getClass() < MaxClass)
            c.liftedCls[bot->getClass()].fetch_add(1, std::memory_order_relaxed);
    }
    return lifted;
}

std::string CombatOverlay(Player* player, int tab, std::string const& overlay)
{
    uint32 const mode = sPlayerbotAIConfig.combatRulePriestOverlay;
    // Part off, or no healing-tree priest: the overlay unchanged, nothing else is read.
    if (!mode || !HealerPriest(player->getClass(), tab))
        return overlay;

    uint32 const guidLow = player->GetGUIDLow();
    unsigned const cohort = ai::cohort::InB(guidLow) ? 1u : 0u;
    PriestOverlayCounters& c = PriestOverlayGlobal();
    c.engines[cohort].fetch_add(1, std::memory_order_relaxed);
    if (!ai::cohort::Applies(mode, guidLow))
        return overlay;
    c.stripped[cohort].fetch_add(1, std::memory_order_relaxed);
    return WithoutEntry(overlay, "+dps");
}

namespace
{
// The solo free bots of the part (as RestGate's RestBot): no group, no real player or real master, free bot, not in a battleground.
bool SoloFree(PlayerbotAI* ai, Player* bot)
{
    return !bot->GetGroup() && !ai->IsRealPlayer() && !ai->HasRealPlayerMaster() && sRandomPlayerbotMgr.IsFreeBot(bot) &&
        !bot->InBattleGround() && bot->IsAlive();
}

unsigned CohortIndex(Player* bot)
{
    return ai::cohort::InB(bot->GetGUIDLow()) ? 1u : 0u;
}
}

RestNeed RestBeforePullNeed(PlayerbotAI* ai)
{
    uint32 const mode = sPlayerbotAIConfig.combatRuleRestBeforePull;
    if (!mode)
        return RestNeed::None;
    Player* const bot = ai->GetBot();
    uint32 const guidLow = bot->GetGUIDLow();
    // RestGate owns the rest of the bots it applies to; the two never act on the same bot.
    if (!ai::cohort::Applies(mode, guidLow) || ai::cohort::Applies(sPlayerbotAIConfig.rotationRestGate, guidLow))
        return RestNeed::None;

    RestReading r;
    r.soloFree = SoloFree(ai, bot);
    r.inCombat = bot->IsInCombat();
    r.attacked = !bot->getAttackers().empty();
    r.hp = uint32(bot->GetHealthPercent());
    r.manaPool = bot->HasMana() && bot->GetMaxPower(POWER_MANA) > 0;
    r.mana = r.manaPool ? uint32(bot->GetPowerPercent(POWER_MANA)) : 100;
    return RestBeforePull(r, sPlayerbotAIConfig.combatRuleRestStartHp, sPlayerbotAIConfig.combatRuleRestStartMana);
}

bool MayPull(PlayerbotAI* ai, Unit* target)
{
    if (!sPlayerbotAIConfig.combatRuleRestBeforePull)
        return true;
    RestNeed const need = RestBeforePullNeed(ai);
    if (need == RestNeed::None)
        return true;
    Player* const bot = ai->GetBot();
    bool const ok = MayPull(need, target && target->GetVictim() == bot);
    if (!ok)
        RestBeforePullGlobal().refused[CohortIndex(bot)].fetch_add(1, std::memory_order_relaxed);
    return ok;
}

void CountPull(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.combatRuleRestBeforePull)
        return;
    Player* const bot = ai->GetBot();
    if (!SoloFree(ai, bot))
        return;
    RestBeforePullCounters& c = RestBeforePullGlobal();
    unsigned const cohort = CohortIndex(bot);
    c.pulls[cohort].fetch_add(1, std::memory_order_relaxed);
    if (uint32(bot->GetHealthPercent()) < sPlayerbotAIConfig.combatRuleRestStartHp)
        c.pullsLowHp[cohort].fetch_add(1, std::memory_order_relaxed);
    if (bot->HasMana() && bot->GetMaxPower(POWER_MANA) > 0 && uint32(bot->GetPowerPercent(POWER_MANA)) < sPlayerbotAIConfig.combatRuleRestStartMana)
        c.pullsLowMana[cohort].fetch_add(1, std::memory_order_relaxed);
}

void CountRest(PlayerbotAI* ai, RestNeed need)
{
    RestBeforePullCounters& c = RestBeforePullGlobal();
    unsigned const cohort = CohortIndex(ai->GetBot());
    if (need == RestNeed::Health)
        c.restHealth[cohort].fetch_add(1, std::memory_order_relaxed);
    else if (need == RestNeed::Mana)
        c.restMana[cohort].fetch_add(1, std::memory_order_relaxed);
}

ProgressLimits ProgressLimitsFromConfig()
{
    ProgressLimits limits;
    limits.deadlineMs = sPlayerbotAIConfig.combatRuleProgressDeadlineSeconds * 1000;
    limits.breakMs = sPlayerbotAIConfig.combatRuleProgressBreakSeconds * 1000;
    return limits;
}

void CountProgress(Player* bot, ProgressStep const& step)
{
    if (!step.episodeStarted && !step.combatMs && !step.kill)
        return;
    ProgressGuardCounters& c = ProgressGuardGlobal();
    unsigned const cohort = CohortIndex(bot);
    uint32 const cls = bot->getClass() < ProgressGuardCounters::Classes ? bot->getClass() : 0;
    unsigned const band = ProgressBand(bot->GetLevel());
    if (step.episodeStarted)
        c.episodes[cohort].fetch_add(1, std::memory_order_relaxed);
    if (step.combatMs)
    {
        c.combatMs[cohort].fetch_add(step.combatMs, std::memory_order_relaxed);
        c.cls[band][cls][ProgressGuardCounters::CombatMs][cohort].fetch_add(step.combatMs, std::memory_order_relaxed);
    }
    if (step.noProgressMs)
    {
        c.noProgressMs[cohort].fetch_add(step.noProgressMs, std::memory_order_relaxed);
        c.cls[band][cls][ProgressGuardCounters::NoProgressMs][cohort].fetch_add(step.noProgressMs, std::memory_order_relaxed);
    }
    if (step.longLoop)
        c.longLoops[cohort].fetch_add(1, std::memory_order_relaxed);
    if (step.kill)
    {
        c.kills[cohort].fetch_add(1, std::memory_order_relaxed);
        c.killMs[cohort].fetch_add(step.killMs, std::memory_order_relaxed);
        c.cls[band][cls][ProgressGuardCounters::Kills][cohort].fetch_add(1, std::memory_order_relaxed);
        c.cls[band][cls][ProgressGuardCounters::KillMs][cohort].fetch_add(step.killMs, std::memory_order_relaxed);
    }
}

void CountCombatPass(Player* bot, bool noOk, bool swing)
{
    ProgressGuardCounters& c = ProgressGuardGlobal();
    unsigned const cohort = CohortIndex(bot);
    uint32 const cls = bot->getClass() < ProgressGuardCounters::Classes ? bot->getClass() : 0;
    unsigned const band = ProgressBand(bot->GetLevel());
    c.cls[band][cls][ProgressGuardCounters::Passes][cohort].fetch_add(1, std::memory_order_relaxed);
    if (noOk)
        c.cls[band][cls][ProgressGuardCounters::NoOk][cohort].fetch_add(1, std::memory_order_relaxed);
    if (noOk && swing)
        c.cls[band][cls][ProgressGuardCounters::NoOkSwing][cohort].fetch_add(1, std::memory_order_relaxed);
}

void CountProgressAct(Player* bot, ProgressAct act)
{
    ProgressGuardCounters& c = ProgressGuardGlobal();
    unsigned const cohort = CohortIndex(bot);
    switch (act)
    {
        case ProgressAct::Backoff: c.backoffs[cohort].fetch_add(1, std::memory_order_relaxed); break;
        case ProgressAct::Woken: c.woken[cohort].fetch_add(1, std::memory_order_relaxed); break;
        case ProgressAct::Break: c.breaks[cohort].fetch_add(1, std::memory_order_relaxed); break;
        case ProgressAct::LoopEvent: c.loopEvents[cohort].fetch_add(1, std::memory_order_relaxed); break;
    }
}

ai::supervisor::Event MakeCombatLoopEvent(Player* bot, std::uint32_t targetEntry, std::uint32_t nowMs, std::uint32_t noProgressMs)
{
    ai::supervisor::Event event;
    event.kind = ai::supervisor::EventKind::CombatLoop;
    event.a = targetEntry;
    event.b = noProgressMs / 1000;
    event.mapId = bot->GetMapId();
    event.x = bot->GetPositionX();
    event.y = bot->GetPositionY();
    event.z = bot->GetPositionZ();
    event.atMs = nowMs;
    event.source = std::uint8_t(ai::supervisor::Requester::CombatGuard);
    return event;
}

namespace
{
unsigned long long Take(std::atomic<std::uint64_t>& counter)
{
    return (unsigned long long)counter.exchange(0, std::memory_order_relaxed);
}

// "[CombatRule] part=HealerStart mode= asked=A/B lifted=A/B cls=<class>:<lifted>" - gate checks the old rule refused for a holy
// paladin / restoration druid, per cohort, and how many the part opened.
void ReportHealerStart()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.combatRuleHealerStart || now < lastReport + 60)
        return;
    lastReport = now;
    HealerStartCounters& c = HealerStartGlobal();
    std::string classes;
    for (uint32 cls = 0; cls < MaxClass; ++cls)
    {
        unsigned long long const n = Take(c.liftedCls[cls]);
        if (!n)
            continue;
        if (!classes.empty())
            classes += ',';
        classes += std::to_string(cls) + ':' + std::to_string(n);
    }
    unsigned long long const askedA = Take(c.asked[0]);
    unsigned long long const askedB = Take(c.asked[1]);
    unsigned long long const liftedA = Take(c.lifted[0]);
    unsigned long long const liftedB = Take(c.lifted[1]);
    sLog.outBasic("[CombatRule] part=HealerStart mode=%u asked=%llu/%llu lifted=%llu/%llu cls=%s",
        sPlayerbotAIConfig.combatRuleHealerStart, askedA, askedB, liftedA, liftedB, classes.empty() ? "-" : classes.c_str());
}

// "[CombatRule] part=PriestOverlay mode= engines=A/B stripped=A/B" - combat engines of discipline / holy priests set up with the
// overlay, per cohort, and how many got it without "+dps".
void ReportPriestOverlay()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.combatRulePriestOverlay || now < lastReport + 60)
        return;
    lastReport = now;
    PriestOverlayCounters& c = PriestOverlayGlobal();
    unsigned long long const enginesA = Take(c.engines[0]);
    unsigned long long const enginesB = Take(c.engines[1]);
    unsigned long long const strippedA = Take(c.stripped[0]);
    unsigned long long const strippedB = Take(c.stripped[1]);
    sLog.outBasic("[CombatRule] part=PriestOverlay mode=%u engines=%llu/%llu stripped=%llu/%llu",
        sPlayerbotAIConfig.combatRulePriestOverlay, enginesA, enginesB, strippedA, strippedB);
}

// "[CombatRule] part=RestBeforePull mode= start_hp= start_mana= pulls=A/B pulls_low_hp=A/B pulls_low_mana=A/B refused=A/B
// rest_ticks_hp=A/B rest_ticks_mana=A/B" - pulls of solo free bots (both cohorts) and of those below the start values, pull checks
// the part refused, ticks the rest node fired.
void ReportRestBeforePull()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.combatRuleRestBeforePull || now < lastReport + 60)
        return;
    lastReport = now;
    RestBeforePullCounters& c = RestBeforePullGlobal();
    unsigned long long v[12];
    std::atomic<std::uint64_t>* all[12] = { &c.pulls[0], &c.pulls[1], &c.pullsLowHp[0], &c.pullsLowHp[1], &c.pullsLowMana[0],
        &c.pullsLowMana[1], &c.refused[0], &c.refused[1], &c.restHealth[0], &c.restHealth[1], &c.restMana[0], &c.restMana[1] };
    for (int i = 0; i < 12; ++i)
        v[i] = Take(*all[i]);
    sLog.outBasic("[CombatRule] part=RestBeforePull mode=%u start_hp=%u start_mana=%u pulls=%llu/%llu pulls_low_hp=%llu/%llu "
        "pulls_low_mana=%llu/%llu refused=%llu/%llu rest_ticks_hp=%llu/%llu rest_ticks_mana=%llu/%llu",
        sPlayerbotAIConfig.combatRuleRestBeforePull, sPlayerbotAIConfig.combatRuleRestStartHp, sPlayerbotAIConfig.combatRuleRestStartMana,
        v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
}

// "[CombatRule] part=ProgressGuard mode= deadline_s= break_s= backoff_ms= ignore_s= episodes=A/B combat_ms=A/B noprog_ms=A/B
// loops5m=A/B backoffs=A/B woken=A/B breaks=A/B loop_events=A/B kills=A/B kill_ms=A/B" and per level band with data
// "[CombatRule] part=ProgressGuardCls mode= band= cls=<class>:<combat_ms A/B>/<noprog_ms A/B>/<kills A/B>/<kill_ms A/B>/<passes A/B>/
// <no_ok A/B>/<no_ok_swing A/B>,..." - both cohorts (the measurement); backoffs, wakes, breaks and loop events only where it acts.
void ReportProgressGuard()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.combatRuleProgressGuard || now < lastReport + 60)
        return;
    lastReport = now;
    ProgressGuardCounters& c = ProgressGuardGlobal();
    unsigned long long v[20];
    std::atomic<std::uint64_t>* all[20] = { &c.episodes[0], &c.episodes[1], &c.combatMs[0], &c.combatMs[1], &c.noProgressMs[0],
        &c.noProgressMs[1], &c.longLoops[0], &c.longLoops[1], &c.backoffs[0], &c.backoffs[1], &c.woken[0], &c.woken[1], &c.breaks[0],
        &c.breaks[1], &c.loopEvents[0], &c.loopEvents[1], &c.kills[0], &c.kills[1], &c.killMs[0], &c.killMs[1] };
    for (int i = 0; i < 20; ++i)
        v[i] = Take(*all[i]);
    sLog.outBasic("[CombatRule] part=ProgressGuard mode=%u deadline_s=%u break_s=%u backoff_ms=%u ignore_s=%u episodes=%llu/%llu "
        "combat_ms=%llu/%llu noprog_ms=%llu/%llu loops5m=%llu/%llu backoffs=%llu/%llu woken=%llu/%llu breaks=%llu/%llu "
        "loop_events=%llu/%llu kills=%llu/%llu kill_ms=%llu/%llu",
        sPlayerbotAIConfig.combatRuleProgressGuard, sPlayerbotAIConfig.combatRuleProgressDeadlineSeconds,
        sPlayerbotAIConfig.combatRuleProgressBreakSeconds, sPlayerbotAIConfig.combatRuleProgressBackoffMs,
        sPlayerbotAIConfig.combatRuleProgressIgnoreSeconds, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11],
        v[12], v[13], v[14], v[15], v[16], v[17], v[18], v[19]);

    for (unsigned band = 0; band < ProgressGuardCounters::Bands; ++band)
    {
        std::string classes;
        for (unsigned cls = 0; cls < ProgressGuardCounters::Classes; ++cls)
        {
            unsigned long long f[ProgressGuardCounters::Fields][2];
            bool any = false;
            for (unsigned field = 0; field < ProgressGuardCounters::Fields; ++field)
                for (unsigned cohort = 0; cohort < 2; ++cohort)
                {
                    f[field][cohort] = Take(c.cls[band][cls][field][cohort]);
                    any = any || f[field][cohort];
                }
            if (!any)
                continue;
            if (!classes.empty())
                classes += ',';
            classes += std::to_string(cls) + ':';
            for (unsigned field = 0; field < ProgressGuardCounters::Fields; ++field)
            {
                if (field)
                    classes += '/';
                classes += std::to_string(f[field][0]) + '/' + std::to_string(f[field][1]);
            }
        }
        if (!classes.empty())
            sLog.outBasic("[CombatRule] part=ProgressGuardCls mode=%u band=%u cls=%s", sPlayerbotAIConfig.combatRuleProgressGuard, band,
                classes.c_str());
    }
}
}

void ReportCombatRule()
{
    ReportHealerStart();
    ReportPriestOverlay();
    ReportRestBeforePull();
    ReportProgressGuard();
}
}
