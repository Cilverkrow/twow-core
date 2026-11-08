#pragma once

#include "playerbot/CombatRulePolicy.h"
#include "playerbot/SupervisorPolicy.h"

#include <string>

class Player;
class Unit;
class PlayerbotAI;

// A3 step 5 (ADR-0041; OB-00 07.11.2026): the combat rule, the parts that read game state. The pure rules are in
// CombatRulePolicy.h; design ws-00/projekt-durchleuchtung/A3-S5-KAMPFREGEL-ENTWURF.md. Every part is off by default
// (AiPlayerbot.CombatRule.<Part> = 0), and then computes exactly the old expression of its call site.
namespace ai::combat_rule
{
// Part HealerStart: the healer gate of attack my target, attack RTI target and attack anything (the old rule: no heal strategy,
// or "offdps" in the combat engine). With AiPlayerbot.CombatRule.HealerStart on for the bot's cohort, a holy paladin or a
// restoration druid passes as well. Called on the bot's thread.
bool HealerMayAttack(PlayerbotAI* ai);

// Part PriestOverlay: the overlay (AiPlayerbot.RandomBotCombatStrategies) AiFactory puts on a free bot's combat engine. With
// AiPlayerbot.CombatRule.PriestOverlay on for the bot's cohort, a discipline or holy priest (tab 0/1) gets it without "+dps",
// which would replace its tree with the shadow placeholder; everything else returns the overlay unchanged.
std::string CombatOverlay(Player* player, int tab, std::string const& overlay);

// Part RestBeforePull: what the bot has to rest for before its next pull (RestNeed::None with the part off, for a cohort it does not
// apply to, and for a bot RestGate applies to). Read by the rest node (strategy "combat rule"), "should eat" and the pull.
RestNeed RestBeforePullNeed(PlayerbotAI* ai);

// The pull bit of attack anything: false while a rest is needed and the target does not fight the bot yet (counted). True with the
// part off, without reading anything else.
bool MayPull(PlayerbotAI* ai, Unit* target);

// A pull starts (attack anything executes): counted for solo free bots of both cohorts while the part is on, with its HP / mana.
void CountPull(PlayerbotAI* ai);

// The rest node fired (counted per tick while the part is on).
void CountRest(PlayerbotAI* ai, RestNeed need);

// Part ProgressGuard (A3 R8): the clock itself is PlayerbotAI::progressClock, read once per engine pass (ObserveCombatProgress) and
// acted on after the pass (ActOnCombatProgress). These are its config limits and counters.
ProgressLimits ProgressLimitsFromConfig();

// One clock reading (both cohorts while the switch is on): episodes, combat time, time without progress, long loops, kills.
void CountProgress(Player* bot, ProgressStep const& step);

// One combat engine pass: without an OK action, and of those while the bot auto-attacks (swing).
void CountCombatPass(Player* bot, bool noOk, bool swing);

enum class ProgressAct : std::uint8_t
{
    Backoff = 0,
    Woken = 1,
    Break = 2,
    LoopEvent = 3,
};

void CountProgressAct(Player* bot, ProgressAct act);

// The event a break leaves for the supervisor's CombatGuard frame (R8 "then an event to the owner"): kind CombatLoop, source
// Requester::CombatGuard, a = target entry, b = seconds without progress, the bot's map and position, atMs = WorldTimer ms. Plain
// data (ADR-0039). The frame takes it with PlayerbotAI::TakeCombatLoopEvent on the bot's thread.
ai::supervisor::Event MakeCombatLoopEvent(Player* bot, std::uint32_t targetEntry, std::uint32_t nowMs, std::uint32_t noProgressMs);

// One "[CombatRule] part=<Part>" line per minute and part that is on (increments); called from the minute report pass.
void ReportCombatRule();
}
