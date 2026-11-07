#include "playerbot/playerbot.h"
#include "playerbot/CombatRule.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
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
}

void ReportCombatRule()
{
    ReportHealerStart();
    ReportPriestOverlay();
}
}
