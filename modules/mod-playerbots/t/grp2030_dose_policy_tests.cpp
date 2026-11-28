// twow-repo#627 GRP2030 dose levers 1 and 2 (pure parts): QuestGroupPolicy.h BetterWish (QuestGroups.Manager.WishScope) and
// GroupDeadQuestPolicy.h (QuestGroups.Manager.DeadQuestEnds / DeadQuestHours).
// twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1; QuestGroups.Manager.WishSkipBlocked): QuestGroupPolicy.h BlockHold
// (ForgetStale / Failed), BlockedKeys and BlockedIn.
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

    // ---- twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1; QuestGroups.Manager.WishSkipBlocked): the queue keeps a block ----
    quest_groups::BlockHold off;
    quest_groups::BlockHold on;
    on.keepBlocked = true;
    on.maxAttempts = 3;
    on.exhaustSeconds = 6 * 3600;
    // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1), ForgetStale: the key off drops a blocked entry not seen for 600 s
    // (old rule); the key on keeps it while blocked and, with attempts, up to the exhaust horizon after its last failure.
    {
        quest_groups::QueueStore q;
        q.Seen(7, 100, t0);
        q.Failed(7, 100, t0, 1800, off);
        q.ForgetStale(t0 + 601, 600, off);
        Require(q.Size() == 0, "key off: a blocked entry not seen for 600 s goes (attempts reset)");
    }
    {
        quest_groups::QueueStore q;
        q.Seen(7, 100, t0);
        q.Failed(7, 100, t0, 1800, on);
        q.Seen(7, 200, t0);
        q.ForgetStale(t0 + 601, 600, on);
        Require(q.Size() == 1 && q.Attempts(7, 100) == 1 && q.Blocked(7, 100, t0 + 601), "key on: the blocked entry stays");
        Require(q.Attempts(7, 200) == 0 && !q.Blocked(7, 200, t0 + 601), "key on: an unblocked stale entry still goes");
        q.ForgetStale(t0 + 1800, 600, on);
        Require(q.Size() == 1 && q.Attempts(7, 100) == 1 && !q.Blocked(7, 100, t0 + 1800),
            "key on: the entry with an attempt stays once its cooldown ran out (no longer blocked)");
    }
    // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): one failure, then the bot is not seen (grouped on another quest, or
    // the wish skipped it): past the cooldown + 600 s the entry stays with its attempt but is not listed (the bot may wish
    // the quest again), and the next failure adds up; past the exhaust horizon after the last failure it goes.
    {
        quest_groups::QueueStore q;
        q.Seen(7, 100, t0);
        q.Failed(7, 100, t0, 1800, on);
        std::uint32_t const later = t0 + 1800 + 601;
        q.ForgetStale(later, 600, on);
        Require(q.Size() == 1 && q.Attempts(7, 100) == 1, "key on: past the cooldown + 600 s the entry and its attempt stay");
        std::vector<std::uint64_t> keys;
        q.BlockedKeys(later, 3, keys);
        Require(keys.empty() && !q.Blocked(7, 100, later), "key on: a kept entry past its cooldown is not listed");
        q.Seen(7, 100, later);
        q.Failed(7, 100, later, 1800, on);
        Require(q.Attempts(7, 100) == 2, "key on: the next failure adds up (attempts 2, not 1)");
        q.ForgetStale(later + 6 * 3600 - 1, 600, on);
        Require(q.Size() == 1 && q.Attempts(7, 100) == 2, "key on: kept up to the exhaust horizon after the last failure");
        q.ForgetStale(later + 6 * 3600 + 601, 600, on);
        Require(q.Size() == 0, "key on: dropped past the exhaust horizon after the last failure");
    }
    // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): the key off, the same history: dropped after 600 s unseen (old rule).
    {
        quest_groups::QueueStore q;
        q.Seen(7, 100, t0);
        q.Failed(7, 100, t0, 1800, off);
        q.ForgetStale(t0 + 600, 600, off);
        Require(q.Size() == 1, "key off: kept while seen within 600 s");
        q.ForgetStale(t0 + 1800 + 601, 600, off);
        Require(q.Size() == 0 && q.Attempts(7, 100) == 0, "key off: past the cooldown + 600 s the entry and its attempt go");
    }
    // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): a lastSeen ahead of now counts as just seen (no unsigned wrap).
    {
        quest_groups::QueueStore q;
        q.Seen(7, 100, t0);
        q.Failed(7, 100, t0, 1800, on);
        q.Seen(7, 100, t0 + 5000);
        q.ForgetStale(t0 + 2000, 600, on);
        Require(q.Size() == 1 && q.Attempts(7, 100) == 1, "key on: a lastSeen ahead of now keeps the entry (cooldown over)");
    }
    // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1), Failed: an exhausted entry gets the exhaust horizon with the key on;
    // off it keeps the cooldown (attempts >= max is the permanent block of QuestBlocked, unchanged).
    {
        quest_groups::QueueStore q;
        for (std::uint32_t i = 0; i < 3; ++i)
            q.Failed(7, 100, t0, 1800, off);
        Require(q.Attempts(7, 100) == 3 && q.Blocked(7, 100, t0 + 1799) && !q.Blocked(7, 100, t0 + 1800),
            "key off: the 3rd failure keeps the retry cooldown only (attempts >= max stays the permanent block)");
        std::vector<std::uint64_t> keys;
        q.BlockedKeys(t0 + 1800, 3, keys);
        Require(keys.size() == 1 && keys[0] == quest_groups::BlockKey(7, 100), "an exhausted entry is listed after its cooldown");
    }
    {
        quest_groups::QueueStore q;
        q.Seen(7, 100, t0);
        q.Failed(7, 100, t0, 1800, on);
        q.Failed(7, 100, t0, 1800, on);
        Require(q.Blocked(7, 100, t0 + 1799) && !q.Blocked(7, 100, t0 + 1800), "key on: below max the retry cooldown as before");
        q.Failed(7, 100, t0, 1800, on);
        Require(q.Attempts(7, 100) == 3 && q.Blocked(7, 100, t0 + 6 * 3600 - 1) && !q.Blocked(7, 100, t0 + 6 * 3600),
            "key on: exhausted = blocked for the exhaust horizon (DeadQuestHours)");
        q.ForgetStale(t0 + 6 * 3600 - 1, 600, on);
        Require(q.Size() == 1, "key on: kept over the horizon although not seen");
        std::vector<std::uint64_t> listed;
        q.BlockedKeys(t0 + 6 * 3600 - 1, 3, listed);
        Require(listed.size() == 1 && listed[0] == quest_groups::BlockKey(7, 100), "key on: exhausted = listed up to the horizon");
        q.ForgetStale(t0 + 6 * 3600 + 601, 600, on);
        Require(q.Size() == 0 && q.Attempts(7, 100) == 0,
            "key on: past the horizon + 600 s the sweep drops it, the quest may be tried again");
        quest_groups::BlockHold shortHorizon = on;
        shortHorizon.exhaustSeconds = 60;
        for (std::uint32_t i = 0; i < 3; ++i)
            q.Failed(8, 100, t0, 1800, shortHorizon);
        Require(q.Blocked(8, 100, t0 + 1799), "the exhaust horizon is never shorter than the retry cooldown");
        quest_groups::BlockHold noMax = on;
        noMax.maxAttempts = 0;
        for (std::uint32_t i = 0; i < 5; ++i)
            q.Failed(9, 100, t0, 1800, noMax);
        Require(!q.Blocked(9, 100, t0 + 1800), "maxAttempts 0: no exhausted entry, the cooldown only");
    }
    // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1), BlockedKeys: the cooldown and the exhausted entries; the sorted list
    // and its lookup (the view's BlockedForBot).
    {
        quest_groups::QueueStore q;
        q.Seen(9, 300, t0);
        q.Failed(9, 300, t0, 1800, on);
        q.Seen(2, 400, t0);
        q.Seen(2, 500, t0);
        q.Failed(2, 500, t0, 1800, on);
        std::vector<std::uint64_t> keys;
        q.BlockedKeys(t0 + 10, 3, keys);
        // twow-repo#627 GRP2030 wish skip (OB-00 28.11. R1): a duplicate (the manager's own cooldown map) and another bot.
        keys.push_back(quest_groups::BlockKey(2, 500));
        keys.push_back(quest_groups::BlockKey(1, 700));
        quest_groups::SortBlockKeys(keys);
        Require(keys.size() == 3 && keys[0] == quest_groups::BlockKey(1, 700) && keys[1] == quest_groups::BlockKey(2, 500) &&
            keys[2] == quest_groups::BlockKey(9, 300), "sorted by bot, then quest, without duplicates");
        Require(quest_groups::BlockedIn(keys, 9, 300) && quest_groups::BlockedIn(keys, 2, 500) && quest_groups::BlockedIn(keys, 1, 700),
            "lookup: hit");
        Require(!quest_groups::BlockedIn(keys, 2, 400) && !quest_groups::BlockedIn(keys, 9, 500) && !quest_groups::BlockedIn(keys, 300, 9) &&
            !quest_groups::BlockedIn(keys, 9, 0), "lookup: miss (an unblocked pair, another bot, swapped halves, quest 0)");
        keys.clear();
        q.BlockedKeys(t0 + 1800, 3, keys);
        Require(keys.empty(), "nothing listed once the cooldowns ran out");
        std::vector<std::uint64_t> none;
        quest_groups::SortBlockKeys(none);
        Require(none.empty() && !quest_groups::BlockedIn(none, 9, 300), "an empty list (the key off) blocks nothing");
    }

    std::cout << "grp2030_dose_policy: ok\n";
    return 0;
}
