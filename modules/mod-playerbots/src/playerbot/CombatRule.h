#pragma once

#include "playerbot/CombatRulePolicy.h"

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

// One "[CombatRule] part=<Part>" line per minute and part that is on (increments); called from the minute report pass.
void ReportCombatRule();
}
