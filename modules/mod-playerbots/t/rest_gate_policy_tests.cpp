// twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): start / end rules with hysteresis, timeout and restart cooldown (also across a
// getMSTime wrap), food / drink durations with the aura cap, the cast retry wait, threshold clamping and the exact [RestGate] lines
// (RestGatePolicy.h). Hand-rolled Require(), std::exit(1) on failure, no gtest.
#ifdef _WIN32
#include <windows.h>
#endif
#include "RestGatePolicy.h"
#include "CohortPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

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

void RequireLine(std::string const& actual, std::string const& expected, char const* message)
{
    if (actual != expected)
    {
        std::cerr << "FAILED: " << message << "\n  expected: " << expected << "\n  actual:   " << actual << '\n';
        std::exit(1);
    }
}

ai::rest_gate::Reading At(std::uint32_t hp, bool manaPool = false, std::uint32_t mana = 100)
{
    ai::rest_gate::Reading r;
    r.hp = hp;
    r.manaPool = manaPool;
    r.mana = mana;
    return r;
}
}

int main()
{
    using namespace ai::rest_gate;
    Thresholds const t;

    // 1) Start: below 70 % HP, or below 40 % mana with a mana pool; never in combat or when attacked.
    {
        Require(StartRest(At(69), t), "69 % HP: rest");
        Require(!StartRest(At(70), t), "70 % HP: no rest (the start line itself)");
        Require(StartRest(At(50), t), "after a revive at 50 %: rest (E6, no own hook)");
        Require(StartRest(At(5), t), "below 20 %: rest (the old food trigger stopped at 20)");
        Require(StartRest(At(95, true, 39), t), "caster at 39 % mana: rest");
        Require(!StartRest(At(95, true, 40), t), "caster at 40 % mana: no rest");
        Require(!StartRest(At(95, false, 0), t), "no mana pool (warrior, druid in form): mana does not count");
        ai::rest_gate::Reading r = At(30);
        r.inCombat = true;
        Require(!StartRest(r, t), "in combat: no rest");
        r = At(30);
        r.attacked = true;
        Require(!StartRest(r, t), "attacked: no rest (who is attacked fights)");
    }

    // 2) End with hysteresis: rest until 90 % HP and 85 % mana; combat ends it; the timeout ends it after MaxRestSeconds.
    {
        Require(EndOf(At(75), t, 1000, 0) == End::None, "75 %: still resting (hysteresis 70 -> 90)");
        Require(EndOf(At(90), t, 1000, 0) == End::Done, "90 %: done");
        Require(EndOf(At(95, true, 80), t, 1000, 0) == End::None, "HP full, mana 80 %: still drinking");
        Require(EndOf(At(95, true, 85), t, 1000, 0) == End::Done, "HP and mana at target: done");
        Require(EndOf(At(95, false, 0), t, 1000, 0) == End::Done, "no mana pool: HP alone");
        ai::rest_gate::Reading r = At(40);
        r.inCombat = true;
        Require(EndOf(r, t, 1000, 0) == End::Combat, "aggro while resting: combat");
        r = At(40);
        r.attacked = true;
        Require(EndOf(r, t, 1000, 0) == End::Combat, "attacked while resting: combat");
        ai::rest_gate::Reading both = At(95);
        both.inCombat = true;
        Require(EndOf(both, t, 1000, 0) == End::Combat, "combat wins over done");
        Require(EndOf(At(40), t, 119999, 0) == End::None, "119.999 s: still resting");
        Require(EndOf(At(40), t, 120000, 0) == End::Timeout, "120 s: timeout");
        Require(EndOf(At(40), t, 0x00001000u, 0xFFFFF000u) == End::None, "timer wrap: 8 s, still resting");
        Require(EndOf(At(40), t, 0x0001D000u, 0xFFFFF000u) == End::Timeout, "timer wrap: 122 s, timeout");
    }

    // 3) Restart cooldown after a timeout: a bot that cannot regenerate is not held for good.
    {
        Require(RestartBlocked(true, 59999, 0), "59.999 s after a timeout: blocked");
        Require(!RestartBlocked(true, 60000, 0), "60 s after a timeout: may rest again");
        Require(!RestartBlocked(false, 1, 0), "after done / combat: no cooldown");
        Require(RestartBlocked(true, 0x00000100u, 0xFFFFFF00u), "timer wrap: blocked");
    }

    // 4) Durations: 500 ms per missing %, capped at the 25 s food / 30 s drink aura.
    {
        Require(DurationMs(10, FoodAuraMs) == 5000, "90 % HP: 5 s of food");
        Require(DurationMs(50, FoodAuraMs) == 25000, "50 % HP: the full 25 s food aura");
        Require(DurationMs(80, FoodAuraMs) == 25000, "20 % HP: capped at the food aura (the trigger fires again)");
        Require(DurationMs(60, DrinkAuraMs) == 30000, "40 % mana: the full 30 s drink aura");
        Require(DurationMs(30, DrinkAuraMs) == 15000, "70 % mana: 15 s of drink");
        Require(DurationMs(0, FoodAuraMs) == 0, "nothing missing: no wait");
        Require(DurationMs(250, DrinkAuraMs) == 30000, "an impossible missing share is capped");
    }

    // 5) Cast retry wait: 2 s after a failed food / drink cast, also across the wrap; nothing without a failure.
    {
        Require(CastRetryWaiting(true, 1999, 0), "1.999 s after a failed cast: wait");
        Require(!CastRetryWaiting(true, 2000, 0), "2 s after a failed cast: try again");
        Require(!CastRetryWaiting(false, 1, 0), "no failure: no wait");
        Require(CastRetryWaiting(true, 0x00000100u, 0xFFFFFF00u), "timer wrap: wait");
    }

    // 6) Clamp: out of range = default, target raised to its start line, seconds bounded.
    {
        Thresholds c;
        c.startHp = 150;
        c.targetHp = 4294967295u;   // a negative config value
        c.startMana = 101;
        c.targetMana = 200;
        c.maxRestSeconds = 0;
        c = Clamp(c);
        Require(c.startHp == 70 && c.targetHp == 90 && c.startMana == 40 && c.targetMana == 85 && c.maxRestSeconds == 120, "defaults");
        Thresholds raised;
        raised.startHp = 80;
        raised.targetHp = 60;
        raised.startMana = 50;
        raised.targetMana = 30;
        raised.maxRestSeconds = 7200;
        raised = Clamp(raised);
        Require(raised.targetHp == 80 && raised.targetMana == 50 && raised.maxRestSeconds == 120, "target below start raised, seconds bounded");
        Thresholds kept;
        kept.startHp = 60;
        kept.targetHp = 100;
        kept.maxRestSeconds = 300;
        kept = Clamp(kept);
        Require(kept.startHp == 60 && kept.targetHp == 100 && kept.maxRestSeconds == 300, "valid values kept");
    }

    // 7) Lines.
    {
        RequireLine(FormatEpisodeLine(4711, "Morwenna", 1, 14, 'B', true, 50, -1, 91, -1, 31000, End::Done, false),
            "[RestGate] bot=4711 name=Morwenna cls=1 level=14 cohort=B applied=1 start_hp=50 start_mana=-1 end_hp=91 end_mana=-1 "
            "dur_ms=31000 reason=done minimal=0", "episode line");
        RequireLine(FormatEpisodeLine(8, "Kettlina", 8, 17, 'A', false, 62, 30, 64, 33, 4000, End::Combat, true),
            "[RestGate] bot=8 name=Kettlina cls=8 level=17 cohort=A applied=0 start_hp=62 start_mana=30 end_hp=64 end_mana=33 "
            "dur_ms=4000 reason=combat minimal=1", "episode line, cohort A");
        Counters c;
        c.episodes = 6;
        c.done = 4;
        c.combat = 1;
        c.death = 2;
        c.timeout = 1;
        c.stale = 3;
        c.restMs = 123456;
        c.castFail = 2;
        RequireLine(FormatTotalLine(60, 'B', 4, c, 3),
            "[RestGateTotal] window_min=60 cohort=B cls=4 episodes=6 done=4 combat=1 death=2 timeout=1 stale=3 rest_s=123 cast_fail=2 dropped=3",
            "total line");
        Require(Counters().Empty() && !c.Empty(), "empty counters");
        Counters failOnly;
        failOnly.castFail = 1;
        Require(!failOnly.Empty(), "a cast failure alone is reported");
        Require(std::string(EndName(End::Timeout)) == "timeout" && std::string(EndName(End::None)) == "none" &&
            std::string(EndName(End::Death)) == "death" && std::string(EndName(End::Stale)) == "stale", "end names");
        std::string const longName(200, 'x');
        Require(FormatEpisodeLine(1, longName, 1, 1, 'A', true, 1, 1, 1, 1, 1, End::Done, false).find(std::string(41, 'x')) == std::string::npos,
            "name cut to 40");
    }

    // 8) Cohort wiring as the value uses it: mode 2 applies only to odd guids, an unknown mode to none.
    {
        Require(ai::cohort::Applies(2, 4711) && !ai::cohort::Applies(2, 8), "mode 2: cohort B only");
        Require(ai::cohort::Applies(1, 8) && !ai::cohort::Applies(0, 4711) && !ai::cohort::Applies(3, 4711), "mode 1 all, 0 / 3 none");
    }

    // 9) The episode state machine (Step / Close), as RestGateValue drives it.
    {
        // A cohort B episode: opens below 70 %, rests while healing, closes as done at 90 %.
        Episode ep;
        Closed c = Step(ep, At(55), t, 1000, true);
        Require(!c.closed && Resting(ep) && ep.startHp == 55, "opens and applies");
        c = Step(ep, At(75), t, 2000, true);
        Require(!c.closed && Resting(ep), "still resting at 75 % (hysteresis)");
        Require(!Step(ep, At(85), t, 16000, true).closed, "still resting at 85 %");
        c = Step(ep, At(91), t, 30000, true);
        Require(c.closed && c.end == End::Done && c.applied && c.durMs == 29000 && c.startHp == 55 && !Resting(ep), "done after 29 s");

        // The same bot in cohort A: tracked with applied=0, never resting.
        Episode a;
        c = Step(a, At(55), t, 1000, false);
        Require(!c.closed && a.open && !Resting(a), "cohort A tracks without resting");

        // Combat start closes on the event (nothing reads the value in combat); no restart cooldown afterwards.
        c = Close(a, End::Combat, 4000);
        Require(c.closed && c.end == End::Combat && !c.applied && c.durMs == 3000 && !a.open && !a.timedOut, "combat event closes A");
        c = Step(a, At(40), t, 9000, false);
        Require(a.open && !c.closed, "a new episode right after the fight (no cooldown after combat)");

        // Death closes on the event; the revived bot (50 %) opens a fresh episode, not a stale timeout.
        Episode d;
        Step(d, At(30), t, 1000, true);
        c = Close(d, End::Death, 5000);
        Require(c.closed && c.end == End::Death && !d.timedOut, "death event closes");
        c = Step(d, At(50), t, 200000, true);
        Require(!c.closed && Resting(d) && d.startMs == 200000, "revive at 50 %: a fresh rest (E6)");

        // An event with no open episode closes nothing.
        Episode none;
        Require(!Close(none, End::Combat, 1).closed, "nothing open: nothing closed");

        // Timeout: 120 s, then a minute without a new episode, then a new one.
        Episode s;
        Step(s, At(40), t, 0, true);
        for (std::uint32_t ms = 10000; ms < 120000; ms += 10000)
            Require(!Step(s, At(40), t, ms, true).closed, "resting within the 120 s");
        c = Step(s, At(40), t, 120000, true);
        Require(c.closed && c.end == End::Timeout && !Resting(s) && s.timedOut, "timeout at 120 s");
        c = Step(s, At(40), t, 150000, true);
        Require(!c.closed && !s.open, "restart cooldown: no new episode");
        c = Step(s, At(40), t, 180000, true);
        Require(s.open && Resting(s), "a new episode after the minute");

        // Stale: not evaluated for more than 15 s (an inactive bot) - dropped without a cooldown, the length runs to the last evaluation.
        Episode g;
        Step(g, At(40), t, 1000, true);
        Step(g, At(45), t, 6000, true);
        c = Step(g, At(45), t, 30000, true);
        Require(c.closed && c.end == End::Stale && c.durMs == 5000 && !g.timedOut, "stale after a 24 s gap");
        Require(g.open && g.startMs == 30000, "the bot still below 70 % opens a fresh episode");

        // Aggro seen on a read (the cached path) closes as combat as well.
        Episode h;
        Step(h, At(40), t, 1000, true);
        ai::rest_gate::Reading hit = At(35);
        hit.attacked = true;
        c = Step(h, hit, t, 2000, true);
        Require(c.closed && c.end == End::Combat && !h.open, "attacked on a read: combat, no new episode while attacked");

        // Timer wrap inside an episode.
        Episode w;
        Step(w, At(40), t, 0xFFFFF000u, true);
        c = Step(w, At(92), t, 0x00002000u, true);
        Require(c.closed && c.end == End::Done && c.durMs == 0x3000u, "durations across the wrap");
    }

    std::cout << "rest_gate_policy_tests passed\n";
    return 0;
}
