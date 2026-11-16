#include "RepeatableTurnInPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#716 BOT-16 (AiPlayerbot.QuestTurnIn.RepeatableAutoComplete): the pure rules of RepeatableTurnInPolicy.h - the mode
// clamp, the cohort split, and which quest of a taker counts as held for a turn-in (complete, unrewarded, auto-complete AND
// repeatable; never a quest the bot could only take again).
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

ai::repeatable_turnin::EnderQuest BloodShard()
{
    // 889 / 5042-5046 as the bot holds them: active, complete in the log, QuestMethod 0, SpecialFlags 1; a repeatable quest is
    // never "rewarded" for Player::GetQuestRewardStatus.
    ai::repeatable_turnin::EnderQuest quest;
    quest.active = true;
    quest.complete = true;
    quest.rewarded = false;
    quest.autoComplete = true;
    quest.repeatable = true;
    return quest;
}
}

int main()
{
    using namespace ai::repeatable_turnin;

    // 1. The mode: 0-3 are valid, everything else is off.
    {
        Require(ValidMode(0) && ValidMode(1) && ValidMode(2) && ValidMode(3), "0..3 are valid");
        Require(!ValidMode(4) && !ValidMode(4294967295u), "other values are invalid");
    }

    // 2. The cohort split: 0 never, 1 every bot, 2 odd low guid (B), 3 even low guid (A), an invalid mode never.
    {
        Require(!Applies(0, 1) && !Applies(0, 2), "mode 0 = off for both cohorts");
        Require(Applies(1, 1) && Applies(1, 2), "mode 1 = every bot");
        Require(Applies(2, 7) && !Applies(2, 8), "mode 2 = cohort B only");
        Require(Applies(3, 8) && !Applies(3, 7), "mode 3 = cohort A only");
        Require(!Applies(4, 7) && !Applies(4, 8) && !Applies(4294967295u, 7), "an invalid mode never applies");
    }

    // 3. The held quest: the Blood Shard case counts.
    {
        Require(HeldForTurnIn(BloodShard()), "complete repeatable auto-complete quest in the log: turn in");
    }

    // 4. Not held: the take-again cases of REWARD_REP (not in the log / not complete) and quests the old rule already covers.
    {
        EnderQuest quest = BloodShard();
        quest.complete = false;
        Require(!HeldForTurnIn(quest), "not complete (not in the log, or incomplete): only take again, no turn-in");

        quest = BloodShard();
        quest.rewarded = true;
        Require(!HeldForTurnIn(quest), "rewarded: no turn-in");

        quest = BloodShard();
        quest.active = false;
        Require(!HeldForTurnIn(quest), "inactive quest: no turn-in (as the dialog status skips it)");

        quest = BloodShard();
        quest.repeatable = false;
        Require(!HeldForTurnIn(quest), "auto-complete, not repeatable: REWARD2 already marks it, not this rule");

        quest = BloodShard();
        quest.autoComplete = false;
        Require(!HeldForTurnIn(quest), "repeatable, not auto-complete: REWARD2 already marks it, not this rule");

        EnderQuest const none;
        Require(!HeldForTurnIn(none), "a default (unknown) quest is never held");
    }

    std::cout << "repeatable_turnin_policy_tests: OK\n";
    return 0;
}
