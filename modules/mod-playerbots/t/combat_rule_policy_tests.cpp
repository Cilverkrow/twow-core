// A3 step 5 (ADR-0041; OB-00 07.11.2026): the combat rule, pure parts (CombatRulePolicy.h).
// Parts HealerStart, PriestOverlay and RestBeforePull (default 0). HealerStart: the healer gate keeps the old rule unchanged and opens only
// for a holy paladin or a restoration druid, and only where the part applies. ProgressGuard (OB-00 08.11.2026, A3 R8): the combat
// progress clock - progress only as damage or approach, per episode, backoff, break, kill and time-to-kill.
#include <cstdlib>
#include <iostream>

#include "CohortPolicy.h"
#include "CombatRulePolicy.h"

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
    using namespace ai::combat_rule;

    // The old rule: no heal strategy, or offdps in the combat engine - whatever the part says.
    for (int applies = 0; applies < 2; ++applies)
        for (int candidate = 0; candidate < 2; ++candidate)
        {
            Require(HealerMayAttack(false, false, applies, candidate), "no heal strategy: may attack");
            Require(HealerMayAttack(true, true, applies, candidate), "healer with offdps: may attack");
        }

    // A healer without offdps: refused unless the part applies to a healer of the part.
    Require(!HealerMayAttack(true, false, false, false), "healer, part off: refused (the old rule)");
    Require(!HealerMayAttack(true, false, false, true), "holy paladin / resto druid, part off: refused (the old rule)");
    Require(!HealerMayAttack(true, false, true, false), "another healer (priest, shaman without offdps), part on: refused");
    Require(HealerMayAttack(true, false, true, true), "holy paladin / resto druid, part on: may attack");

    // Which healers the part is for.
    Require(HealerWithoutOffdpsCreator(ClassPaladin, true, false), "paladin with holy");
    Require(HealerWithoutOffdpsCreator(ClassDruid, false, true), "druid with restoration");
    Require(!HealerWithoutOffdpsCreator(ClassPaladin, false, true), "paladin without holy (restoration is no paladin tree)");
    Require(!HealerWithoutOffdpsCreator(ClassDruid, true, false), "druid without restoration");
    Require(!HealerWithoutOffdpsCreator(5, true, false), "priest (5) holy: has an offdps creator, not the part's");
    Require(!HealerWithoutOffdpsCreator(7, false, true), "shaman (7) restoration: has an offdps creator (#653), not the part's");
    Require(!HealerWithoutOffdpsCreator(ClassPaladin, false, false), "retribution / protection paladin");
    Require(!HealerWithoutOffdpsCreator(ClassDruid, false, false), "balance / feral druid");

    // Modes come from CohortPolicy: 0 off, 1 all, 2 cohort B (odd guid), 3 cohort A (even guid), other = off.
    Require(!ai::cohort::Applies(0, 3) && ai::cohort::Applies(1, 4), "mode 0 off, mode 1 all");
    Require(ai::cohort::Applies(2, 3) && !ai::cohort::Applies(2, 4), "mode 2 = cohort B only");
    Require(ai::cohort::Applies(3, 4) && !ai::cohort::Applies(3, 3), "mode 3 = cohort A only");
    Require(!ai::cohort::Applies(4, 3) && !ai::cohort::Applies(4, 4), "unknown mode is off");

    // Counters start at zero and count up.
    HealerStartCounters& c = HealerStartGlobal();
    Require(c.asked[0].load() == 0 && c.lifted[1].load() == 0 && c.liftedCls[ClassDruid].load() == 0, "counters start at 0");
    c.lifted[1].fetch_add(2);
    Require(c.lifted[1].exchange(0) == 2 && c.lifted[1].load() == 0, "counters are increments");
    Require(ClassDruid < MaxClass, "the druid fits the class counters");

    // Part PriestOverlay: discipline / holy priests only; exact-entry removal like Engine::ChangeStrategy (split on ',', no trim).
    Require(HealerPriest(ClassPriest, 0) && HealerPriest(ClassPriest, 1), "discipline and holy priests");
    Require(!HealerPriest(ClassPriest, 2), "shadow priest: +dps changes nothing");
    Require(!HealerPriest(ClassPaladin, 0) && !HealerPriest(ClassDruid, 1), "other classes");
    Require(WithoutEntry("+dps,+dps assist,-threat", "+dps") == "+dps assist,-threat", "the profile overlay loses only +dps");
    Require(WithoutEntry("-threat,+custom::say", "+dps") == "-threat,+custom::say", "the code default has no +dps: unchanged");
    Require(WithoutEntry("+dps assist", "+dps") == "+dps assist", "+dps assist is another entry");
    Require(WithoutEntry("-threat,+dps", "+dps") == "-threat", "+dps at the end");
    Require(WithoutEntry("+dps", "+dps") == "", "+dps alone");
    Require(WithoutEntry("+dps,+dps,-threat", "+dps") == "-threat", "every +dps entry");
    Require(WithoutEntry(" +dps,-threat", "+dps") == " +dps,-threat", "no trim: the engine would not trim either");
    Require(WithoutEntry("", "+dps") == "", "empty overlay");
    Require(WithoutEntry("a,,b", "+dps") == "a,,b", "empty entries kept");

    PriestOverlayCounters& p = PriestOverlayGlobal();
    Require(p.engines[0].load() == 0 && p.stripped[1].load() == 0, "overlay counters start at 0");
    p.stripped[1].fetch_add(1);
    Require(p.stripped[1].exchange(0) == 1 && p.stripped[1].load() == 0, "overlay counters are increments");

    // Part RestBeforePull: solo free bots out of combat, health first, mana only with a pool, the pull bit.
    RestReading r;
    r.soloFree = true;
    r.hp = 59;
    Require(RestBeforePull(r, 60, 40) == RestNeed::Health, "HP below start: rest for health");
    r.hp = 10;
    Require(RestBeforePull(r, 60, 40) == RestNeed::Health, "no lower bound (the < 20 % gap)");
    r.hp = 60;
    Require(RestBeforePull(r, 60, 40) == RestNeed::None, "HP at start: no rest");
    r.manaPool = true;
    r.mana = 39;
    Require(RestBeforePull(r, 60, 40) == RestNeed::Mana, "mana below start with a pool: rest for mana");
    r.hp = 30;
    Require(RestBeforePull(r, 60, 40) == RestNeed::Health, "health before mana");
    r.hp = 90;
    Require(RestBeforePull(r, 60, 0) == RestNeed::None, "StartMana 0: no mana rest");
    r.manaPool = false;
    Require(RestBeforePull(r, 60, 40) == RestNeed::None, "no mana pool (cat / bear form, warrior, rogue): no mana rest");
    r.hp = 20;
    RestReading inCombat = r;
    inCombat.inCombat = true;
    Require(RestBeforePull(inCombat, 60, 40) == RestNeed::None, "in combat: the rule does not rest");
    RestReading attacked = r;
    attacked.attacked = true;
    Require(RestBeforePull(attacked, 60, 40) == RestNeed::None, "attacked: no rest");
    RestReading grouped = r;
    grouped.soloFree = false;
    Require(RestBeforePull(grouped, 60, 40) == RestNeed::None, "group / real player / battleground: not the part's bot");

    Require(MayPull(RestNeed::None, false), "no rest needed: may pull");
    Require(!MayPull(RestNeed::Health, false) && !MayPull(RestNeed::Mana, false), "rest needed: no pull");
    Require(MayPull(RestNeed::Health, true), "a target that fights the bot is always taken");

    Require(ClampStartHp(0) == 1 && ClampStartHp(60) == 60 && ClampStartHp(150) == 99, "StartHp 1..99");
    Require(ClampStartMana(-5) == 0 && ClampStartMana(40) == 40 && ClampStartMana(100) == 99, "StartMana 0..99");

    RestBeforePullCounters& rc = RestBeforePullGlobal();
    Require(rc.pulls[0].load() == 0 && rc.refused[1].load() == 0 && rc.restMana[1].load() == 0, "rest counters start at 0");
    rc.refused[1].fetch_add(4);
    Require(rc.refused[1].exchange(0) == 4 && rc.refused[1].load() == 0, "rest counters are increments");

    // Part ProgressGuard (A3 R8): progress = damage to the target or approach; OK does not count; clock per episode.
    {
        ProgressLimits lim;   // deadline 15 s, break 45 s, gain 1 yd, window 8 s, long 300 s
        ProgressClock c;
        ProgressReading r;
        r.inCombat = false;
        r.nowMs = 1000;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Off && !c.active, "out of combat: no episode");

        r.inCombat = true;
        r.target = 7;
        r.targetAlive = true;
        r.targetHealth = 100;
        r.distance = 30.0f;
        ProgressStep s = ObserveProgress(c, r, lim);
        Require(s.episodeStarted && s.verdict == ProgressVerdict::Progress && c.active, "combat starts an episode");

        r.nowMs = 2000;
        r.distance = 29.5f;
        s = ObserveProgress(c, r, lim);
        Require(s.verdict == ProgressVerdict::Quiet && s.combatMs == 1000, "half a yard closer is no progress");
        r.nowMs = 3000;
        r.distance = 28.9f;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Progress, "one yard below the best distance is progress");
        r.nowMs = 4000;
        r.distance = 40.0f;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Quiet, "moving away is no progress");
        r.nowMs = 5000;
        r.distance = 28.5f;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Quiet, "coming back is no progress (best 28.9)");
        r.nowMs = 6000;
        r.targetHealth = 90;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Progress, "damage to the target is progress");
        r.nowMs = 7000;
        r.targetHealth = 95;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Quiet, "regeneration is no progress");
        r.nowMs = 8000;
        r.targetHealth = 94;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Progress, "a drop from the regenerated value counts");

        // No progress: quiet up to the deadline, then backoff, break at 45 s and again every 45 s; window and long loop.
        r.nowMs = 8000 + 14999;
        s = ObserveProgress(c, r, lim);
        Require(s.verdict == ProgressVerdict::Quiet && s.noProgressMs == 14999 - 8000, "inside the deadline; time beyond the window counts");
        r.nowMs = 8000 + 15000;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Backoff, "deadline reached: backoff");
        r.target = 8;   // a target switch does not restart the clock
        r.targetHealth = 200;
        r.nowMs = 8000 + 30000;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Backoff && c.engageMs == 38000, "switch: no progress, no restart");
        r.nowMs = 8000 + 45000;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Break, "break at BreakMs");
        r.nowMs = 8000 + 60000;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Backoff, "after a break: backoff until the next break");
        r.nowMs = 8000 + 90000;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Break, "again every BreakMs");
        r.nowMs = 8000 + 299999;
        Require(!ObserveProgress(c, r, lim).longLoop, "below 5 min: no long loop");
        r.nowMs = 8000 + 300000;
        Require(ObserveProgress(c, r, lim).longLoop, "5 min without progress: one long loop");
        r.nowMs = 8000 + 301000;
        Require(!ObserveProgress(c, r, lim).longLoop, "the long loop counts once per stretch");

        // Kill: the target read dead, or the old target dead after a switch; time-to-kill from its first reading.
        r.nowMs = 400000;
        r.targetAlive = false;
        s = ObserveProgress(c, r, lim);
        Require(s.kill && s.killMs == 400000 - 38000 && s.verdict == ProgressVerdict::Progress, "dead target: kill with time-to-kill");
        r.nowMs = 401000;
        Require(!ObserveProgress(c, r, lim).kill, "a kill counts once");
        r.target = 9;
        r.targetAlive = true;
        r.targetHealth = 50;
        r.nowMs = 402000;
        ObserveProgress(c, r, lim);
        r.target = 10;
        r.previousTargetDied = true;
        r.nowMs = 410000;
        s = ObserveProgress(c, r, lim);
        Require(s.kill && s.killMs == 8000, "switch after the old target died: kill");
        r.previousTargetDied = false;
        r.target = 0;   // no target in combat: no progress possible
        r.nowMs = 411000;
        Require(ObserveProgress(c, r, lim).verdict == ProgressVerdict::Quiet, "no target: quiet");

        r.inCombat = false;
        r.nowMs = 412000;
        s = ObserveProgress(c, r, lim);
        Require(s.episodeEnded && s.verdict == ProgressVerdict::Off && !c.active && s.combatMs == 1000, "combat end closes the episode");
        r.inCombat = true;
        r.target = 11;
        r.nowMs = 500000;
        Require(ObserveProgress(c, r, lim).episodeStarted && c.lastProgressMs == 500000, "a new episode starts a new clock");

        // Wrap of the ms timer.
        ProgressClock w;
        ProgressReading wr;
        wr.inCombat = true;
        wr.target = 1;
        wr.targetAlive = true;
        wr.targetHealth = 10;
        wr.nowMs = 0xFFFFF000u;
        ObserveProgress(w, wr, lim);
        wr.nowMs = 0x00001000u;
        Require(ObserveProgress(w, wr, lim).combatMs == 0x2000u, "wrap-safe differences");

        Require(ProgressShouldWake(c, 12, 100, 90, 50) && ProgressShouldWake(c, 11, 0, 90, 50) && ProgressShouldWake(c, 11, 100, 40, 50),
            "wake on switch, damage or low health");
        Require(!ProgressShouldWake(c, 11, c.targetHealth, 90, 50), "no wake while nothing changed");

        Require(ClampProgressDeadline(1) == 5 && ClampProgressDeadline(15) == 15 && ClampProgressDeadline(999) == 300, "deadline 5..300");
        Require(ClampProgressBreak(10, 15) == 15 && ClampProgressBreak(45, 15) == 45 && ClampProgressBreak(999, 15) == 600, "break deadline..600");
        Require(ClampProgressBackoffMs(-1) == 0 && ClampProgressBackoffMs(1000) == 1000 && ClampProgressBackoffMs(9000) == 5000, "backoff 0..5000");
        Require(ClampProgressIgnore(1) == 10 && ClampProgressIgnore(120) == 120 && ClampProgressIgnore(999) == 600, "ignore 10..600");
        Require(ProgressBand(1) == 0 && ProgressBand(9) == 0 && ProgressBand(10) == 1 && ProgressBand(19) == 1 && ProgressBand(20) == 2, "bands");

        ProgressGuardCounters& pc = ProgressGuardGlobal();
        Require(pc.breaks[1].load() == 0 && pc.cls[2][11][ProgressGuardCounters::NoOkSwing][1].load() == 0, "guard counters start at 0");
        pc.breaks[1].fetch_add(3);
        Require(pc.breaks[1].exchange(0) == 3 && pc.breaks[1].load() == 0, "guard counters are increments");
    }

    std::cout << "combat_rule_policy_tests: ok\n";
    return 0;
}
