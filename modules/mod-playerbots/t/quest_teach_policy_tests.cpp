// twow-repo#578 (b) (AiPlayerbot.AutoLearnQuestTeachEffects): teaching RewSpells teach their targets, retired quests nothing,
// switch off = as before, priest list either way.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "QuestTeachPolicy.h"

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
    using namespace ai::quest_teach;

    // 5671 Disguise: effect 36 (learn 5170) + 36 (learn 5669); a normal spell has none.
    Require(HasLearnEffect(36, 36, 0) && HasLearnEffect(0, 0, 36), "learn effect in any slot");
    Require(!HasLearnEffect(6, 0, 0), "negative: an aura spell is no teaching spell");

    Require(IsRetiredQuest(60011, "[CANCELLED] Piercing Light") && IsRetiredQuest(60012, "Piercing Light"), "Piercing Light");
    Require(IsRetiredQuest(80388, "Stay Awhile and Listen...") && IsRetiredQuest(12345, "[DEPRECATED] Old"), "deprecated");
    Require(!IsRetiredQuest(50004, "The Orcish Anatomy"), "50004 is live");

    // Switch on: 50004 (RewSpell 5671) teaches its targets; retired quests skip; plain RewSpell as before.
    Require(ForRewSpell(true, false, false, true) == RewSpellMode::TeachEffects, "on: teaching spell -> targets");
    Require(ForRewSpell(true, true, false, true) == RewSpellMode::Skip, "on: retired quest teaches nothing");
    Require(ForRewSpell(true, false, false, false) == RewSpellMode::LearnDirect, "on: plain spell learned as before");

    // Switch off: exactly the old behaviour (negative: the bug stays reproducible for A/B).
    Require(ForRewSpell(false, false, false, true) == RewSpellMode::LearnDirect, "off: teaching spell stored as before");
    Require(ForRewSpell(false, true, false, true) == RewSpellMode::LearnDirect, "off: retired quests as before");

    // Priest list (e.g. 19350 Starshards) teaches its targets with the switch off and on.
    Require(ForRewSpell(false, false, true, true) == RewSpellMode::TeachEffects &&
        ForRewSpell(true, false, true, true) == RewSpellMode::TeachEffects, "priest list either way");

    std::cout << "quest_teach_policy_tests passed\n";
    return 0;
}
