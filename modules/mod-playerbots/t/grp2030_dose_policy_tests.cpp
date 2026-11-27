// twow-repo#627 GRP2030 dose levers 1 and 2 (pure parts): QuestGroupPolicy.h BetterWish (QuestGroups.Manager.WishScope) and
// GroupDeadQuestPolicy.h (QuestGroups.Manager.DeadQuestEnds / DeadQuestHours).
#include "GroupDeadQuestPolicy.h"
#include "QuestGroupPolicy.h"

#include <cstdlib>
#include <iostream>
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

ai::quest_groups::WishPick Pick(bool help, std::uint32_t open, std::uint32_t questId)
{
    ai::quest_groups::WishPick p;
    p.any = true;
    p.help = help;
    p.open = open;
    p.questId = questId;
    return p;
}
}

int main()
{
    using namespace ai;

    // ---- lever 1: the wish order ----
    quest_groups::WishPick none;
    Require(quest_groups::BetterWish(none, false, 1, 500), "no wish yet: any waiting quest is taken");
    Require(quest_groups::BetterWish(none, true, 1, 500), "no wish yet: a group-help quest is taken");
    // Scope 0 is the old rule: group-help quests only, more open objectives win, a tie keeps the earlier slot.
    Require(quest_groups::BetterWish(Pick(true, 2, 300), true, 3, 900), "more open objectives win (old rule)");
    Require(!quest_groups::BetterWish(Pick(true, 3, 300), true, 3, 100), "a tie keeps the earlier slot (old rule, not the lower id)");
    Require(!quest_groups::BetterWish(Pick(true, 3, 300), true, 2, 100), "fewer open objectives lose");
    // Scope 1: a group-help quest always beats a normal one; between normal quests the lowest id.
    Require(quest_groups::BetterWish(Pick(false, 4, 100), true, 1, 900), "a group-help quest beats a normal one");
    Require(!quest_groups::BetterWish(Pick(true, 1, 900), false, 4, 100), "a normal quest never beats a group-help one");
    Require(quest_groups::BetterWish(Pick(false, 1, 500), false, 1, 400), "between normal quests the lower id wins");
    Require(!quest_groups::BetterWish(Pick(false, 1, 400), false, 5, 500), "a higher id loses even with more open objectives");
    // The same quest log in any slot order gives the same wish (the shared order).
    {
        std::uint32_t const ids[3] = { 783, 255, 1001 };
        quest_groups::WishPick a, b;
        for (int i = 0; i < 3; ++i)
            if (quest_groups::BetterWish(a, false, 1, ids[i]))
                a = Pick(false, 1, ids[i]);
        for (int i = 2; i >= 0; --i)
            if (quest_groups::BetterWish(b, false, 1, ids[i]))
                b = Pick(false, 1, ids[i]);
        Require(a.questId == 255 && b.questId == 255, "slot order does not change the normal wish");
    }

    // ---- lever 2: the dead-quest store ----
    Require(dead_quest::ClampHours(0) == 6 && dead_quest::ClampHours(-3) == 6, "hours 0 / negative = 6");
    Require(dead_quest::ClampHours(1) == 1 && dead_quest::ClampHours(48) == 48 && dead_quest::ClampHours(49) == 48, "hours 1..48");

    dead_quest::Store s;
    std::uint32_t const t0 = 100000;
    // Off: nothing kept.
    Require(s.End(255, true, false, t0, 0, 6) == dead_quest::Note::Off, "ends 0 = off");
    Require(!s.Dead(255, t0) && s.Count(255) == 0, "off keeps nothing");
    // Three dead ends in a row make the quest dead for 6 h.
    Require(s.End(255, true, false, t0, 3, 6) == dead_quest::Note::Counted, "1st dead end counted");
    Require(s.End(255, true, false, t0 + 900, 3, 6) == dead_quest::Note::Counted, "2nd dead end counted");
    Require(s.End(255, false, false, t0 + 1000, 3, 6) == dead_quest::Note::Ignored, "too_small etc. leave the count");
    Require(s.Count(255) == 2, "count kept over an ignored end");
    Require(s.End(255, true, false, t0 + 1800, 3, 6) == dead_quest::Note::NowDead, "3rd dead end: dead");
    Require(s.Dead(255, t0 + 1800) && s.Dead(255, t0 + 1800 + 6 * 3600 - 1), "dead for 6 h");
    Require(!s.Dead(255, t0 + 1800 + 6 * 3600), "alive again after 6 h");
    Require(s.End(255, true, false, t0 + 2000, 3, 6) == dead_quest::Note::AlreadyDead, "a late group of a dead quest");
    // Progress resets the count.
    Require(s.End(60131, true, false, t0, 3, 6) == dead_quest::Note::Counted, "other quest counted");
    Require(s.End(60131, true, false, t0, 3, 6) == dead_quest::Note::Counted, "other quest counted twice");
    Require(s.End(60131, false, true, t0, 3, 6) == dead_quest::Note::Reset, "progress resets");
    Require(s.Count(60131) == 0 && !s.Dead(60131, t0), "reset: count 0, not dead");
    // The view list: only dead quests; expired entries without a count are dropped.
    std::vector<std::uint32_t> dead;
    s.DeadList(t0 + 1800, dead);
    Require(dead.size() == 1 && dead[0] == 255, "view lists the dead quest only");
    s.DeadList(t0 + 1800 + 7 * 3600, dead);
    Require(dead.empty(), "nothing dead after expiry");
    Require(s.Count(255) == 0, "the expired entry is gone");
    s.Clear();
    Require(!s.Dead(255, t0 + 1800), "clear");

    std::cout << "grp2030_dose_policy: ok\n";
    return 0;
}
