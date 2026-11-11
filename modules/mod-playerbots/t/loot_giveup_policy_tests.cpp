#include "LootGiveUpPolicy.h"
#include "CohortPolicy.h"

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
    using namespace ai::loot_giveup;
    std::uint64_t const a = 0xF110000000123456ull, b = 0xF130000000654321ull;

    // The constants of design v2 (4e9c26b5).
    Require(WindowSeconds == 30 && MinTries == 5 && MinImproveYards == 1.0f && MaxGapSeconds == 5 && ExcludeSeconds == 600 &&
        MaxBlocked == 16, "design constants");

    // Mode 0 = never (no cohort applies); the loader clamps > 3 to 0 (contract).
    Require(!ai::cohort::Applies(0, 1) && !ai::cohort::Applies(0, 2), "mode 0: never");
    Require(ai::cohort::Applies(2, 1) && !ai::cohort::Applies(2, 2), "mode 2: cohort B only");

    // A stuck approach: no progress for 30 s with >= 5 tries gives up; not before.
    {
        Try t;
        Require(Next(t, a, 78.0f, 1000) == Step::Restart, "first try: restart");
        Require(t.tries == 1 && t.distStart == 78.0f && t.distBest == 78.0f && t.begun == 1000, "state of the first try");
        std::int64_t now = 1000;
        for (int i = 0; i < 5; ++i)
        {
            now += 5;
            Require(Next(t, a, 78.0f, now) == Step::Keep, "no progress, < 30 s: keep");
        }
        Require(now == 1025 && t.tries == 6, "6 tries in 25 s");
        Require(Next(t, a, 78.0f, 1030) == Step::GiveUp, "30 s without progress, 7 tries: give up");
        Require(t.distStart == 78.0f && t.distBest == 78.0f && t.begun == 1000, "line fields kept");
    }
    // Fewer than 5 tries since the clock started: no give-up, even when 30 s are over (the clock is set back by hand).
    {
        Try t;
        Next(t, a, 50.0f, 0);
        Require(Next(t, a, 50.0f, 5) == Step::Keep, "try 2");
        Require(Next(t, a, 50.0f, 10) == Step::Keep, "try 3");
        Require(t.tries == 3, "3 tries");
        t.since = -30;   // the clock began 30 s before the first try (a progress before): 30 s are over
        Require(Next(t, a, 50.0f, 12) == Step::Keep, "4 tries after 42 s: keep (< 5 tries)");
        Require(Next(t, a, 50.0f, 14) == Step::GiveUp, "the 5th try: give up");
    }
    // A gap > 5 s (the bot did something else) restarts; exactly 5 s does not.
    {
        Try t;
        Next(t, a, 40.0f, 100);
        Require(Next(t, a, 40.0f, 105) == Step::Keep, "gap 5 s: keep");
        Require(Next(t, a, 40.0f, 111) == Step::Restart, "gap 6 s: restart");
        Require(t.tries == 1 && t.since == 111 && t.begun == 111, "restart resets the clock and the tries");
    }
    // A new guid restarts.
    {
        Try t;
        Next(t, a, 40.0f, 100);
        Next(t, a, 40.0f, 104);
        Require(Next(t, b, 20.0f, 106) == Step::Restart, "another loot target: restart");
        Require(t.guid == b && t.distStart == 20.0f && t.tries == 1, "the new approach");
    }
    // >= 1 yd better than the best resets the clock; < 1 yd does not; a worse distance does not move the best.
    {
        Try t;
        Next(t, a, 40.0f, 0);
        Require(Next(t, a, 39.5f, 5) == Step::Keep, "0.5 yd: no progress");
        Require(t.distBest == 40.0f, "best not moved by < 1 yd");
        Require(Next(t, a, 39.0f, 10) == Step::Progress, "1 yd: progress");
        Require(t.distBest == 39.0f && t.since == 10 && t.tries == 1, "progress restarts the clock");
        Require(Next(t, a, 45.0f, 15) == Step::Keep, "a worse distance: keep");
        Require(t.distBest == 39.0f && t.distStart == 40.0f, "best and start kept");
        std::int64_t now = 15;
        Step s = Step::Keep;
        while (now < 40)
        {
            now += 5;
            s = Next(t, a, 39.0f, now);
        }
        Require(now == 40 && s == Step::GiveUp, "30 s after the last progress: give up");
        Require(t.begun == 0, "waited_s counts from the first try");
    }
    // A walking bot (>= 1 yd per try) never gives up.
    {
        Try t;
        float d = 200.0f;
        Next(t, a, d, 0);
        for (std::int64_t now = 2; now < 400; now += 2)
        {
            d -= 1.5f;
            Require(Next(t, a, d, now) == Step::Progress, "walking: progress");
        }
    }

    // The exclusion list: blocked until the expiry, renewal, eviction of the oldest at 16, the gauge deltas.
    {
        BlockList list;
        Require(list.Size() == 0 && !list.Blocks(a, 0), "empty list blocks nothing");
        Require(list.Add(a, 1600, 1000) == 1, "add: +1");
        Require(list.Blocks(a, 1000) && list.Blocks(a, 1599), "blocked before the expiry");
        Require(!list.Blocks(a, 1600) && !list.Blocks(b, 1000), "not blocked at the expiry, other guid not blocked");
        Require(list.Add(a, 1700, 1100) == 0 && list.Size() == 1 && list.Blocks(a, 1650), "renewal: +0, longer");
        Require(list.Purge(1699) == 0 && list.Purge(1700) == -1 && list.Size() == 0, "purge at the expiry: -1");
    }
    {
        BlockList list;
        int held = 0;
        for (std::uint64_t i = 1; i <= 16; ++i)
            held += list.Add(i, std::int64_t(1000 + i), 0);
        Require(held == 16 && list.Size() == 16, "16 entries held");
        held += list.Add(100, 2000, 0);
        Require(held == 16 && list.Size() == 16, "full: the 17th evicts, the gauge stays");
        Require(!list.Blocks(1, 0) && list.Blocks(2, 0) && list.Blocks(100, 0), "the oldest (smallest until) left");
        held += list.Add(200, 3000, 1010);   // entries 2..10 (until 1002..1010) expired
        Require(held == list.Size() && list.Size() == 8, "expired entries leave on add, the gauge follows");
        Require(list.Blocks(200, 1010) && list.Blocks(11, 1010), "live entries stay");
    }

    // The minute line: one per minute, give-ups taken, the gauge never below 0.
    {
        Counters c;
        Require(c.LogDue(60) && !c.LogDue(119) && c.LogDue(120), "once a minute");
        c.giveUps.fetch_add(3);
        Require(c.TakeGiveUps() == 3 && c.TakeGiveUps() == 0, "give-ups taken and reset");
        c.held.fetch_add(2);
        Require(c.Held() == 2, "held");
        c.held.fetch_sub(5);
        Require(c.Held() == 0, "never below 0");
    }

    std::cout << "loot_giveup_policy_tests: OK\n";
    return 0;
}
