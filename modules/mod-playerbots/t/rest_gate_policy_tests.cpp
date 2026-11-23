// twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): start / end rules with hysteresis, timeout and restart cooldown (also across a
// getMSTime wrap), food / drink durations with the aura cap, the cast retry wait, threshold clamping and the exact [RestGate] lines
// (RestGatePolicy.h). Hand-rolled Require(), std::exit(1) on failure, no gtest.
// DEEP7 finding 3 (20.11.2026, measurement only): the late read of a stale episode (late / gap / dur_late, the stale_* counters), and a
// randomized check that Step / Close decide exactly as the code before that change (verbatim copy in namespace ref below).
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

// The episode decisions before the DEEP7 measurement change (f4d0ef98 / 3cd8dac9), copied verbatim: the new code must decide the same.
namespace ref
{
using namespace ai::rest_gate;

struct Closed
{
    bool closed = false;
    End end = End::None;
    bool applied = false;
    std::uint32_t durMs = 0;
    std::uint32_t startHp = 0;
    int startMana = -1;
};

inline Closed Close(Episode& ep, End end, std::uint32_t nowMs)
{
    Closed c;
    if (!ep.open)
        return c;
    c.closed = true;
    c.end = end;
    c.applied = ep.applied;
    // A stale episode lasted until it was last seen, not until the bot came back.
    c.durMs = (end == End::Stale ? ep.lastSeenMs : nowMs) - ep.startMs;
    c.startHp = ep.startHp;
    c.startMana = ep.startMana;
    ep.open = false;
    ep.timedOut = end == End::Timeout;
    ep.endMs = nowMs;
    return c;
}

inline Closed Step(Episode& ep, Reading const& r, Thresholds const& t, std::uint32_t nowMs, bool applies)
{
    Closed c;
    if (ep.open)
    {
        End const end = std::uint32_t(nowMs - ep.lastSeenMs) > StaleGapMs ? End::Stale : EndOf(r, t, nowMs, ep.startMs);
        if (end == End::None)
        {
            ep.lastSeenMs = nowMs;
            return c;
        }
        c = ref::Close(ep, end, nowMs);   // the one change to the copy: ref:: (ADL would also find ai::rest_gate::Close)
    }
    if (RestartBlocked(ep.timedOut, nowMs, ep.endMs) || !StartRest(r, t))
        return c;
    ep.open = true;
    ep.applied = applies;
    ep.timedOut = false;
    ep.startMs = nowMs;
    ep.lastSeenMs = nowMs;
    ep.startHp = r.hp;
    ep.startMana = r.manaPool ? int(r.mana) : -1;
    return c;
}

// The old RestGateTrace RecordEpisode body (now ai::rest_gate::Count).
inline void Record(Counters& c, End end, std::uint32_t durMs)
{
    ++c.episodes;
    c.restMs += durMs;
    if (end == End::Done)
        ++c.done;
    else if (end == End::Combat)
        ++c.combat;
    else if (end == End::Death)
        ++c.death;
    else if (end == End::Stale)
        ++c.stale;
    else if (end == End::Timeout)
        ++c.timeout;
}
}

namespace
{
bool SameEpisode(ai::rest_gate::Episode const& a, ai::rest_gate::Episode const& b)
{
    return a.open == b.open && a.applied == b.applied && a.timedOut == b.timedOut && a.startMs == b.startMs &&
        a.lastSeenMs == b.lastSeenMs && a.endMs == b.endMs && a.startHp == b.startHp && a.startMana == b.startMana;
}

bool SameClosed(ai::rest_gate::Closed const& n, ref::Closed const& o)
{
    return n.closed == o.closed && n.end == o.end && n.applied == o.applied && n.durMs == o.durMs && n.startHp == o.startHp &&
        n.startMana == o.startMana;
}

std::uint32_t g_seed = 0x2B8E1F3Du;

std::uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;   // LCG, deterministic
    return g_seed >> 8;
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
            "[RestGateTotal] window_min=60 cohort=B cls=4 episodes=6 done=4 combat=1 death=2 timeout=1 stale=3 rest_s=123 cast_fail=2 dropped=3"
            " stale_rested=0 stale_combat=0 stale_timeout=0 stale_low=0 stale_gap_s=0 offer_s=0 giveback_s=0",
            "total line (the old fields first and unchanged, the stale_* fields after dropped=, RG-B1 offer_s / giveback_s last)");
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
        Require(c.late == End::None && c.gapMs == 24000 && c.lateMs == 29000, "stale: late read still below target, gap 24 s, 29 s to it");
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
        Require(c.late == End::None && c.gapMs == 0x3000u && c.lateMs == 0x3000u, "not stale: no late reading, dur_late = dur");
    }

    // 10) DEEP7 finding 3 (measurement only): the late read of a stale episode, its line fields and counters.
    {
        // The eat / drink wait: opened at 55 % (first read), the AI waits DurationMs (22.5 s), the next read finds 100 %.
        Episode e;
        Step(e, At(55), t, 1000, true);
        Closed c = Step(e, At(100), t, 23500, true);
        Require(c.closed && c.end == End::Stale && c.durMs == 0 && c.late == End::Done && c.gapMs == 22500 && c.lateMs == 22500 &&
            !e.open && !e.timedOut, "rested through the wait: stale (as before), late = rested, dur_ms 0, dur_late 22.5 s");

        // A stale caster at 99 % HP, 50 % mana: still below the 85 % mana target, within 120 s - low; no new episode (above 40 %).
        Episode m;
        Step(m, At(95, true, 30), t, 0, true);
        c = Step(m, At(99, true, 50), t, 16000, true);
        Require(c.end == End::Stale && c.late == End::None && !m.open, "stale, late = low, no reopen above the start lines");

        // Seen in combat at the late read.
        Episode k;
        Step(k, At(40), t, 0, false);
        ai::rest_gate::Reading hit = At(60);
        hit.inCombat = true;
        c = Step(k, hit, t, 20000, false);
        Require(c.end == End::Stale && c.late == End::Combat && !c.applied && !k.open, "stale, late = combat (cohort A)");

        // Parked for 200 s, still low: late = timeout; still no cooldown (stale ends as stale), a fresh episode opens.
        Episode p;
        Step(p, At(40), t, 0, true);
        c = Step(p, At(40), t, 200000, true);
        Require(c.end == End::Stale && c.late == End::Timeout && !p.timedOut && p.open && p.startMs == 200000,
            "stale, late = timeout, no cooldown, fresh episode");

        // Exactly StaleGapMs is not stale (unchanged boundary).
        Episode b;
        Step(b, At(40), t, 0, true);
        Require(!Step(b, At(41), t, StaleGapMs, true).closed, "a 15 s gap is not stale");
        c = Step(b, At(95), t, 2 * StaleGapMs + 1, true);
        Require(c.end == End::Stale && c.late == End::Done && c.gapMs == StaleGapMs + 1 && c.durMs == StaleGapMs &&
            c.lateMs == 2 * StaleGapMs + 1, "15.001 s: stale, rested");

        // Events never carry a late reading.
        Episode v;
        Step(v, At(40), t, 0, true);
        c = Close(v, End::Combat, 30000);
        Require(c.end == End::Combat && c.late == End::None && c.durMs == 30000 && c.gapMs == 30000 && c.lateMs == 30000, "combat event");

        // Lines: non-stale lines stay byte-identical (also with late fields passed), a stale line gets late / gap_ms / dur_late_ms.
        RequireLine(FormatEpisodeLine(4711, "Morwenna", 1, 14, 'B', true, 50, -1, 91, -1, 31000, End::Done, false, End::Done, 7, 9),
            "[RestGate] bot=4711 name=Morwenna cls=1 level=14 cohort=B applied=1 start_hp=50 start_mana=-1 end_hp=91 end_mana=-1 "
            "dur_ms=31000 reason=done minimal=0", "done line unchanged");
        RequireLine(FormatEpisodeLine(4711, "Morwenna", 1, 14, 'B', true, 55, -1, 100, -1, 0, End::Stale, false, End::Done, 22500, 22500),
            "[RestGate] bot=4711 name=Morwenna cls=1 level=14 cohort=B applied=1 start_hp=55 start_mana=-1 end_hp=100 end_mana=-1 "
            "dur_ms=0 reason=stale minimal=0 late=rested gap_ms=22500 dur_late_ms=22500", "stale line");
        RequireLine(FormatEpisodeLine(8, "Kettlina", 8, 17, 'A', false, 62, 30, 99, 50, 5, End::Stale, true, End::None, 16000, 16005),
            "[RestGate] bot=8 name=Kettlina cls=8 level=17 cohort=A applied=0 start_hp=62 start_mana=30 end_hp=99 end_mana=50 "
            "dur_ms=5 reason=stale minimal=1 late=low gap_ms=16000 dur_late_ms=16005", "stale line, still low");
        Require(std::string(LateName(End::Combat)) == "combat" && std::string(LateName(End::Timeout)) == "timeout", "late names");
        std::string const longName(200, 'x');
        std::string const longLine = FormatEpisodeLine(4294967295u, longName, 4294967295u, 4294967295u, 'B', true, 4294967295u, -2147483647,
            4294967295u, -2147483647, 4294967295u, End::Stale, true, End::Timeout, 4294967295u, 4294967295u);
        Require(longLine.size() < 384 && longLine.find("dur_late_ms=4294967295") != std::string::npos, "the longest stale line fits");

        // Counters: stale stays the sum, the late split adds up to it, the gaps are summed.
        Counters n;
        Closed s1;
        s1.closed = true;
        s1.end = End::Stale;
        s1.durMs = 10;
        s1.late = End::Done;
        s1.gapMs = 22500;
        Count(n, s1);
        s1.late = End::None;
        s1.gapMs = 16000;
        Count(n, s1);
        s1.late = End::Combat;
        Count(n, s1);
        s1.late = End::Timeout;
        Count(n, s1);
        Closed d1;
        d1.closed = true;
        d1.end = End::Done;
        d1.durMs = 8000;
        d1.gapMs = 1000;
        Count(n, d1);
        Require(n.episodes == 5 && n.stale == 4 && n.done == 1 && n.staleRested == 1 && n.staleLow == 1 && n.staleCombat == 1 &&
            n.staleTimeout == 1 && n.staleGapMs == 22500 + 3 * 16000 && n.restMs == 8040, "counters");
        RequireLine(FormatTotalLine(60, 'A', 8, n, 0),
            "[RestGateTotal] window_min=60 cohort=A cls=8 episodes=5 done=1 combat=0 death=0 timeout=0 stale=4 rest_s=8 cast_fail=0 dropped=0"
            " stale_rested=1 stale_combat=1 stale_timeout=1 stale_low=1 stale_gap_s=70 offer_s=0 giveback_s=0", "total line with the stale split");
    }

    // 10b) RG-B1 (Rotation.RestGate.StaleGiveBack, OB-10 review e20a5abf, Q2 (ii')): the give-back of an applied close. One case per
    //      late kind of a stale close, the cap, the non-stale closes and the switch off; then the offer_s / giveback_s counters.
    {
        Thresholds const cap;   // MaxRestSeconds 120
        Closed s;
        s.closed = true;
        s.applied = true;
        s.end = End::Stale;
        s.durMs = 1;
        s.gapMs = 28541;
        s.lateMs = 28542;
        s.late = End::Done;
        Require(GiveBackMs(s, cap, true) == 28542, "stale, late = rested: the time to the late read (dur_late_ms)");
        Require(GiveBackMs(s, cap, false) == 1, "switch off: durMs as before (about 0)");
        s.late = End::None;
        Require(GiveBackMs(s, cap, true) == 28542, "stale, late = low: the time to the late read");
        s.late = End::Combat;
        Require(GiveBackMs(s, cap, true) == 1, "stale, late = combat: durMs (no combat time back)");
        s.late = End::Timeout;
        s.lateMs = 200000;
        Require(GiveBackMs(s, cap, true) == 1, "stale, late = timeout: durMs");
        s.late = End::Done;
        Require(GiveBackMs(s, cap, true) == 120000, "stale, rested after 200 s: capped at MaxRestSeconds");
        s.lateMs = 120000;
        Require(GiveBackMs(s, cap, true) == 120000, "exactly at the cap");
        s.lateMs = 119999;
        Require(GiveBackMs(s, cap, true) == 119999, "just under the cap");
        Thresholds shortCap;
        shortCap.maxRestSeconds = 20;
        s.lateMs = 28542;
        Require(GiveBackMs(s, shortCap, true) == 20000, "the cap follows the configured MaxRestSeconds");
        for (End e : { End::Done, End::Combat, End::Death, End::Timeout })
        {
            Closed o;
            o.closed = true;
            o.applied = true;
            o.end = e;
            o.durMs = 30000;
            o.gapMs = 30000;
            o.lateMs = 30000;
            o.late = End::None;
            Require(GiveBackMs(o, cap, true) == 30000 && GiveBackMs(o, cap, false) == 30000, "a non-stale close: durMs with and without the switch");
        }

        Counters g;
        CountGiveBack(g, 28542, 28542);
        CountGiveBack(g, 28542, 0);   // no timed WORK / TRAVEL phase: offered, nothing added
        CountGiveBack(g, 1, 1);
        Require(g.offerMs == 57085 && g.giveBackMs == 28543 && g.episodes == 0 && g.Empty(), "give-back counters, no episode counted");
        RequireLine(FormatTotalLine(60, 'A', 5, g, 0),
            "[RestGateTotal] window_min=60 cohort=A cls=5 episodes=0 done=0 combat=0 death=0 timeout=0 stale=0 rest_s=0 cast_fail=0 dropped=0"
            " stale_rested=0 stale_combat=0 stale_timeout=0 stale_low=0 stale_gap_s=0 offer_s=57 giveback_s=28", "total line ends with offer_s / giveback_s");
        Counters big;
        big.episodes = big.done = big.combat = big.death = big.timeout = big.stale = big.restMs = big.castFail = 18446744073709551615ull;
        big.staleRested = big.staleCombat = big.staleTimeout = big.staleLow = big.staleGapMs = 18446744073709551615ull;
        big.offerMs = big.giveBackMs = 18446744073709551615ull;
        std::string const longTotal = FormatTotalLine(4294967295u, 'B', 4294967295u, big, 4294967295u);
        Require(longTotal.size() < 640 && longTotal.find(" giveback_s=18446744073709551") != std::string::npos, "the longest total line fits");
    }

    // 11) Byte-identical decisions: random readings, gaps (also around StaleGapMs and the 120 s timeout), events and the timer wrap,
    // driven through the new and the old Step / Close side by side; every episode field, Resting and the closed episode must match, and
    // the counters' old fields must match the old RecordEpisode.
    {
        Thresholds tt[3];
        tt[1].startHp = 50;
        tt[1].targetHp = 100;
        tt[1].maxRestSeconds = 20;
        tt[2].startMana = 90;
        tt[2].targetMana = 95;
        tt[2].maxRestSeconds = 3600;
        std::uint32_t const gaps[] = { 0, 1, 100, 1000, 5000, 14999, 15000, 15001, 16000, 22500, 30000, 59999, 60000, 119999, 120000,
            200000 };
        std::uint64_t stepsChecked = 0, staleSeen = 0, eventsSeen = 0;
        for (int run = 0; run < 3000; ++run)
        {
            Thresholds const& th = tt[run % 3];
            Episode a, b;
            Counters cn, co;
            std::uint32_t now = (run % 7 == 0) ? 0xFFFF0000u + (Next() & 0xFFFFu) : Next();
            bool const applies = (Next() & 1u) != 0;
            for (int i = 0; i < 400; ++i)
            {
                std::uint32_t const pick = Next();
                now += (pick % 4 == 0) ? gaps[(pick >> 4) % (sizeof(gaps) / sizeof(gaps[0]))] : (pick >> 4) % 3000;
                ai::rest_gate::Reading r;
                r.hp = Next() % 101;
                r.manaPool = (Next() % 3) != 0;
                r.mana = Next() % 101;
                r.inCombat = (Next() % 17) == 0;
                r.attacked = (Next() % 23) == 0;
                std::uint32_t const op = Next() % 20;
                if (op == 0 || op == 1)
                {
                    End const ev = op == 0 ? End::Combat : End::Death;   // OnCombatStarted / OnDeath
                    Closed const cn1 = Close(a, ev, now);
                    ref::Closed const co1 = ref::Close(b, ev, now);
                    Require(SameClosed(cn1, co1) && cn1.late == End::None, "event close: same");
                    if (cn1.closed)
                    {
                        Count(cn, cn1);
                        ref::Record(co, co1.end, co1.durMs);
                        ++eventsSeen;
                    }
                }
                else if (op == 2)
                {
                    a.open = false;   // Calculate: switch off, dead or out of the population
                    b.open = false;
                }
                else
                {
                    bool const ap = (op == 3) ? !applies : applies;
                    Closed const cn1 = Step(a, r, th, now, ap);
                    ref::Closed const co1 = ref::Step(b, r, th, now, ap);
                    Require(SameClosed(cn1, co1), "Step: same closed episode");
                    Require(cn1.end == End::Stale || cn1.late == End::None, "late only on stale");
                    if (cn1.closed)
                    {
                        Count(cn, cn1);
                        ref::Record(co, co1.end, co1.durMs);
                        if (cn1.end == End::Stale)
                        {
                            ++staleSeen;
                            Require(cn1.gapMs > StaleGapMs && cn1.lateMs == cn1.durMs + cn1.gapMs, "stale: gap and dur_late");
                        }
                    }
                }
                Require(SameEpisode(a, b), "same episode state");
                Require(Resting(a) == Resting(b), "same Resting");
                ++stepsChecked;
            }
            Require(cn.episodes == co.episodes && cn.done == co.done && cn.combat == co.combat && cn.death == co.death &&
                cn.timeout == co.timeout && cn.stale == co.stale && cn.restMs == co.restMs && cn.castFail == co.castFail,
                "old counter fields unchanged");
            Require(cn.staleRested + cn.staleCombat + cn.staleTimeout + cn.staleLow == cn.stale, "stale split adds up");
        }
        Require(stepsChecked == 1200000 && staleSeen > 1000 && eventsSeen > 1000, "the random check covered stale closes and events");
    }

    std::cout << "rest_gate_policy_tests passed\n";
    return 0;
}
