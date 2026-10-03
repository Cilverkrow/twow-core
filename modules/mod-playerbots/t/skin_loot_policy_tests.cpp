#include "SkinLootPolicy.h"

#include <cstdlib>
#include <cstring>
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

bool Is(ai::skin_loot::ClearTrace const& trace, char const* state, char const* reason, uint32_t detail)
{
    return std::strcmp(trace.state, state) == 0 && std::strcmp(trace.reason, reason) == 0 && trace.detail == detail;
}
}

int main()
{
    using namespace ai::skin_loot;

    // Core rule (Spell.cpp, SPELL_EFFECT_SKINNING): below skill 100 a corpse needs
    // (level - 10) * 10, from skill 100 on level * 5.
    Require(RequiredSkinningSkill(5, 1) <= 1, "skill 1 skins level 5");
    Require(RequiredSkinningSkill(10, 1) == 0, "level 10 needs skill 0");
    Require(RequiredSkinningSkill(11, 1) == 10 && RequiredSkinningSkill(11, 1) > 1, "level 11 needs 10, more than skill 1");
    Require(RequiredSkinningSkill(15, 50) == 50, "level 15 needs 50");
    Require(RequiredSkinningSkill(20, 99) == 100, "below skill 100 level 20 needs 100");
    Require(RequiredSkinningSkill(20, 120) == 100, "from skill 100 on level 20 needs level * 5");
    Require(RequiredSkinningSkill(21, 100) == 105, "skill 100 already uses level * 5");

    // Clear the corpse only when every condition holds.
    Require(ShouldClearCorpseForSkinning(true, true, true, true, true, true, 50, 15), "all conditions met");
    Require(ShouldClearCorpseForSkinning(true, true, true, true, true, true, 1, 10), "skill 1 skins level 10");
    Require(!ShouldClearCorpseForSkinning(false, true, true, true, true, true, 50, 15), "switch off: legacy loot rules");
    Require(!ShouldClearCorpseForSkinning(true, false, true, true, true, true, 50, 15), "not a roster bot on its own");
    Require(!ShouldClearCorpseForSkinning(true, true, false, true, true, true, 50, 15), "no corpse loot (skin or pickpocket loot)");
    Require(!ShouldClearCorpseForSkinning(true, true, true, false, true, true, 50, 15), "corpse not skinnable");
    Require(!ShouldClearCorpseForSkinning(true, true, true, true, false, true, 50, 15), "bot without skinning");
    Require(!ShouldClearCorpseForSkinning(true, true, true, true, true, false, 50, 15), "no skinning knife");
    Require(!ShouldClearCorpseForSkinning(true, true, true, true, true, true, 1, 11), "skill too low for the corpse");
    Require(!ShouldClearCorpseForSkinning(true, true, true, true, true, true, 49, 15), "one point short");
    Require(ShouldClearCorpseForSkinning(true, true, true, true, true, true, 120, 24), "skill 120 skins level 24 (needs 120)");
    Require(!ShouldClearCorpseForSkinning(true, true, true, true, true, true, 120, 25), "skill 120 is short for level 25 (needs 125)");

    // Loot left on the corpse: never a skinning target.
    Require(!IsSkinTarget(true, true), "lootable corpse is no skinning target");
    Require(IsSkinTarget(false, true), "looted skinnable corpse is a skinning target");
    Require(!IsSkinTarget(false, false), "corpse not skinnable");
    Require(!IsSkinTarget(true, false), "lootable, not skinnable");

    // The line after a clear: junk_taken only when the switch stored junk, detail = that count.
    Require(Is(TraceAfterClear(true, 5, 2), "cleared", "junk_taken", 2), "emptied with junk: junk_taken, detail = junk stored");
    Require(Is(TraceAfterClear(true, 1, 1), "cleared", "junk_taken", 1), "one junk item is enough");
    Require(Is(TraceAfterClear(true, 3, 0), "cleared", "nothing_blocked", 3), "every item allowed anyway: no junk_taken");
    Require(Is(TraceAfterClear(true, 0, 0), "cleared", "nothing_blocked", 0), "money only: no junk_taken");
    Require(Is(TraceAfterClear(false, 4, 1), "skipped", "loot_left", 4), "loot left: skipped even with junk taken");
    Require(Is(TraceAfterClear(false, 0, 0), "skipped", "loot_left", 0), "nothing taken, loot left");

    std::cout << "skin_loot_policy_tests passed\n";
    return 0;
}
