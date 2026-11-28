// AiPlayerbot.QuestElite.Defer (WS30-ELITE-QUEST-01): the mode check and clamps, the population, the scope (Type 1 or an elite
// kill objective, never dungeon / raid), the sealed scope table, the rule (1a group floor, 1b solo margin, 99 = 1b off), the
// wish floor, the form floor and the per-cohort minute line (EliteQuestPolicy.h). Hand-rolled Require(), std::exit(1) on
// failure, no gtest, no game library.
#include "EliteQuestPolicy.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
    using namespace ai::elite_quest;

    // 1) The mode: 0..3 valid; anything else is off. Clamps: MinGroup 2..5, SoloLevelsAbove 0..99.
    {
        Require(ValidMode(0) && ValidMode(1) && ValidMode(2) && ValidMode(3), "modes 0..3 valid");
        Require(!ValidMode(4) && !ValidMode(99), "mode 4+ invalid");
        Require(ClampMinGroup(0) == 2 && ClampMinGroup(1) == 2 && ClampMinGroup(3) == 3 && ClampMinGroup(9) == 5, "MinGroup clamp");
        Require(ClampSoloLevelsAbove(0) == 0 && ClampSoloLevelsAbove(5) == 5 && ClampSoloLevelsAbove(500) == 99, "SoloLevelsAbove clamp");
        Require(DefaultMinGroup == 3 && DefaultSoloLevelsAbove == 5, "defaults 3 / +5");
    }

    // 2) The population: roster bot on its own, in the mode's cohort (B = odd low guid).
    {
        Require(!Applies(0, 7, true) && !Applies(0, 8, true), "mode 0 off");
        Require(Applies(1, 7, true) && Applies(1, 8, true), "mode 1 all");
        Require(Applies(2, 7, true) && !Applies(2, 8, true), "mode 2 cohort B");
        Require(!Applies(3, 7, true) && Applies(3, 8, true), "mode 3 cohort A");
        Require(!Applies(4, 7, true) && !Applies(4, 8, true), "invalid mode off");
        Require(!Applies(1, 7, false), "not a roster bot on its own (real player master) -> never");
    }

    // 3) Scope: Type 1, or an elite kill objective; dungeon (81) / raid (62) never; a normal quest without elites never.
    {
        Require(InScope(1, false) && InScope(1, true), "type 1 elite");
        Require(InScope(0, true), "type 0 with an elite kill objective");
        Require(!InScope(0, false), "type 0 without elites");
        Require(!InScope(81, true) && !InScope(62, true) && !InScope(81, false), "dungeon / raid never");
        Require(!EliteRank(0) && EliteRank(1) && EliteRank(2) && EliteRank(3) && !EliteRank(4), "rank set 1/2/3");
        Require(EffectiveLevel(19, 20) == 20 && EffectiveLevel(19, 18) == 19 && EffectiveLevel(19, 0) == 19, "effective level");
        Require(EffectiveLevel(-1, 0) == 0 && EffectiveLevel(0, 12) == 12, "scaling quest level");
        std::vector<ScopeEntry> table = {{255, 20}, {87, 9}, {255, 99}, {3, 5}};
        Seal(table);
        Require(table.size() == 3 && table[0].questId == 3 && table[1].questId == 87 && table[2].questId == 255, "seal sorts and dedups");
        Require(table[2].level == 20, "seal keeps the first entry of a duplicate");
        Require(Find(table, 255) && Find(table, 255)->level == 20 && Find(table, 3) && !Find(table, 4) && !Find(table, 256), "lookup");
        Require(!Find(std::vector<ScopeEntry>(), 255), "empty table");
    }

    // 4) The rule, quest 255 (QuestLevel 19), MinGroup 3, +5.
    {
        Require(!Pursue(1, 18, 18, 19, 3, 5), "solo L18 deferred");
        Require(!Pursue(1, 23, 23, 19, 3, 5), "solo L23 deferred (1b needs >= 24)");
        Require(Pursue(1, 24, 24, 19, 3, 5), "solo L24 pursues (1b)");
        Require(!Pursue(2, 19, 19, 19, 3, 5), "group of 2 at L19 deferred (1a needs 3)");
        Require(Pursue(3, 19, 20, 19, 3, 5), "group of 3, lowest L19 pursues (1a)");
        Require(!Pursue(3, 18, 21, 19, 3, 5), "group of 3, lowest L18 deferred (one member below QL blocks the group, OB-20 (b))");
        Require(Pursue(2, 18, 24, 19, 3, 5), "L24 in a group of 2 pursues by 1b");
        Require(!Pursue(1, 60, 60, 19, 3, 99), "99 turns 1b off");
        Require(Pursue(3, 19, 19, 19, 3, 99), "99 leaves 1a on");
        Require(!Pursue(1, 19, 19, 19, 1, 99) && Pursue(2, 19, 19, 19, 1, 99), "MinGroup clamps to >= 2 (1 -> 2: a group of 2 passes, a solo bot not)");
        Require(Pursue(5, 19, 19, 19, 9, 99) && !Pursue(4, 19, 19, 19, 9, 99), "MinGroup clamps to <= 5");
    }

    // 5) Wish floor (E3) and form floor (E4).
    {
        Require(!MayWish(18, 19) && MayWish(19, 19) && MayWish(30, 19), "wish only at >= quest level");
        Require(!MayForm(2, 19, 19, 3, 5), "no group of 2 at QL");
        Require(MayForm(3, 19, 19, 3, 5), "group of 3 at QL");
        Require(!MayForm(4, 18, 19, 3, 5), "group of 4 with one member below QL");
        Require(MayForm(2, 24, 19, 3, 5), "group of 2, all >= QL+5 (1b)");
        Require(!MayForm(2, 24, 19, 3, 99), "1b off with 99");
    }

    // 6) The minute line: per cohort, omitted when empty, fixed field order.
    {
        std::array<std::uint32_t, SlotCount> counts{};
        Require(FormatMinuteLine(counts).empty(), "empty when all 0");
        counts[Slot(Event::TravelDeferred, 8)] += 4;
        counts[Slot(Event::FormRefused, 7)] += 1;
        counts[Slot(Event::Pursued, 7)] += 2;
        Require(FormatMinuteLine(counts) ==
            "[QuestElite] minute travel_deferred_a=4 travel_deferred_b=0 target_skipped_a=0 target_skipped_b=0 wish_skipped_a=0 "
            "wish_skipped_b=0 form_refused_a=0 form_refused_b=1 pursued_a=0 pursued_b=2", "minute line text");
        Require(Slot(Event::WishSkipped, 8) == 4 && Slot(Event::WishSkipped, 9) == 5, "slot layout");
    }

    // 7) Config validity (ultracode review 28.11.): out of range is invalid (the loader then uses the default, not the bound).
    {
        Require(!ValidMinGroup(-1) && !ValidMinGroup(0) && !ValidMinGroup(1) && ValidMinGroup(2) && ValidMinGroup(5) && !ValidMinGroup(6),
            "MinGroup valid 2..5 only");
        Require(!ValidSoloLevelsAbove(-5) && ValidSoloLevelsAbove(0) && ValidSoloLevelsAbove(99) && !ValidSoloLevelsAbove(100),
            "SoloLevelsAbove valid 0..99 only");
    }

    // 8) The entry sets: deferred minus everything a pursued / other quest needs; sorted, unique.
    {
        std::vector<std::int32_t> deferred = {1180, 1178, 1179, 1178, 300};
        std::vector<std::int32_t> pursued = {77, 55, 77};
        std::vector<std::int32_t> keep = {300, 12, 300};   // 300: also an item dropper of a pursued quest -> stays needed
        FinishEntrySets(deferred, pursued, keep);
        Require(deferred == std::vector<std::int32_t>({1178, 1179, 1180}), "deferred minus keep, sorted, unique");
        Require(pursued == std::vector<std::int32_t>({55, 77}), "pursued sorted, unique");
        std::vector<std::int32_t> none, none2, none3;
        FinishEntrySets(none, none2, none3);
        Require(none.empty() && none2.empty(), "empty sets");
    }

    // 9) The rule label and the reward line (quest 255 at its effective level 20, MinGroup 3, +5).
    {
        Require(std::string(RuleLabel(3, 20, 21, 20, 3, 5)) == "1a", "label 1a");
        Require(std::string(RuleLabel(1, 25, 25, 20, 3, 5)) == "1b", "label 1b");
        Require(std::string(RuleLabel(2, 19, 19, 20, 3, 5)) == "none", "label none");
        Require(std::string(RuleLabel(3, 25, 25, 20, 3, 5)) == "1a", "1a wins when both hold");
        RewardFacts f;
        f.guid = 7;
        f.questId = 255;
        f.questLevel = 20;
        f.botLevel = 25;
        f.members = 1;
        f.minLevel = 25;
        f.judged = true;
        Require(FormatRewardLine(f, 3, 5) ==
            "[QuestElite] reward guid=7 cohort=b quest=255 level=20 bot_level=25 members=1 min_level=25 rule=1b judged=1 formed=0",
            "reward line text");
        f.guid = 8;
        f.formed = true;
        f.judged = false;
        Require(FormatRewardLine(f, 3, 99).find(" cohort=a ") != std::string::npos &&
            FormatRewardLine(f, 3, 99).find(" rule=none judged=0 formed=1") != std::string::npos, "control row, 1b off");
    }

    std::cout << "elite_quest_policy: all tests passed\n";
    return 0;
}
