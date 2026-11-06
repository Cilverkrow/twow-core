// twow-repo#602 ROT-01 W24 duel hang (DuelRulePolicy.h, OB-00 06.11.2026, Rotation.DuelRule default 1, HARM per ADR-0041 R4): no rpg duel
// against a group member, accept only as the challenged player, a running duel engages the opponent, a bot-vs-bot duel without a fight
// is cancelled after Rotation.DuelRule.CancelSeconds.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "DuelRulePolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace ai::duel_rule;

    // 1) Modes: 0 off, 1 every bot, 2 cohort B (odd low guid); unknown values never reach the policy (PlayerbotAIConfig maps them to 1).
    Require(!Applies(0, 10) && !Applies(0, 11), "mode 0: no bot");
    Require(Applies(1, 10) && Applies(1, 11), "mode 1: every bot");
    Require(!Applies(2, 10) && Applies(2, 11), "mode 2: cohort B only");

    // 2) rpg duel: a group member is skipped (Mojama 877 challenged Rokuzan 148 of its own ad-hoc group, 520 min lost each).
    Require(SkipGroupDuel(true, true), "group member: no duel");
    Require(!SkipGroupDuel(true, false), "a bot outside the group: duel as before");
    Require(!SkipGroupDuel(false, true), "switch off: as before");

    // 3) accept duel: only the challenged player accepts (the core ignores the initiator's accept).
    Require(SkipInitiatorAccept(true, true), "initiator: no accept");
    Require(!SkipInitiatorAccept(true, false), "challenged: accepts");
    Require(!SkipInitiatorAccept(false, true), "switch off: the initiator 'accepts' as before");

    // 4) Engage: a running duel (countdown over, not finished) whose opponent is not the current target.
    Require(EngageOpponent(true, true, false, false), "running duel, other or no target: engage");
    Require(!EngageOpponent(true, true, false, true), "already on the opponent: nothing to do");
    Require(!EngageOpponent(true, false, false, false), "requested / countdown: the opponent is not attackable yet");
    Require(!EngageOpponent(true, true, true, false), "finished duel: no engage");
    Require(!EngageOpponent(false, true, false, false), "switch off: only the old 'my attacker count' rule");

    // 5) Quiet clock: starts at the first quiet second, keeps it, resets on a fight or without a duel.
    Require(NextQuietSince(0, true, false, 100) == 100, "first quiet second");
    Require(NextQuietSince(100, true, false, 130) == 100, "the clock keeps its start");
    Require(NextQuietSince(100, true, true, 130) == 0, "a fight resets the clock");
    Require(NextQuietSince(100, false, false, 130) == 0, "no duel: no clock");

    // 6) Cancel: bot-vs-bot only, after cancelSeconds, 0 seconds = never.
    Require(!CancelDue(true, true, 100, 159, 60), "59 s quiet: not yet");
    Require(CancelDue(true, true, 100, 160, 60), "60 s quiet: cancel");
    Require(!CancelDue(true, false, 100, 1000, 60), "a real player's duel is never cancelled");
    Require(!CancelDue(true, true, 0, 1000, 60), "no quiet clock (fighting): no cancel");
    Require(!CancelDue(true, true, 100, 100000, 0), "cancelSeconds 0: never");
    Require(!CancelDue(false, true, 100, 1000, 60), "switch off: never");

    std::cout << "duel_rule_policy_tests passed\n";
    return 0;
}
