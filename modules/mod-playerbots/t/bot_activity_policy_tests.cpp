// twow-repo#601 R01 L1 (AiPlayerbot.ActivityTrace): BotActivityPolicy.h - kinds, the per-bot clock, the lines and the
// minute counters.
#include "BotActivityPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
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
    using namespace ai::bot_activity;

    // Bands: L0-4, L5-9, L10-14 (the R01 bands), L60 and above in the last one.
    Require(BandOf(0) == 0 && BandOf(4) == 0 && BandOf(5) == 1, "L0-4 band 0, L5 band 1");
    Require(BandOf(10) == 2 && BandOf(14) == 2 && BandOf(15) == 3 && BandOf(24) == 4, "R01 bands 10-14 / 15-19 / 20-24");
    Require(BandOf(59) == 11 && BandOf(60) == Bands - 1 && BandOf(63) == Bands - 1, "L60 and above in the last band");

    // Travel status: PREPARE..WORK active, NONE / COOLDOWN / EXPIRED not.
    Require(!TravelActive(0) && TravelActive(1) && TravelActive(2) && TravelActive(3) && TravelActive(4), "1-4 active");
    Require(!TravelActive(5) && !TravelActive(6) && !TravelActive(7), "cooldown, expired and above not active");

    // Kinds: parked, dead, combat, then the target (stuck / grind_idle first), a flight; idle is what is left.
    auto make = [](std::uint8_t status, Dest dest, float moved = 50.0f) {
        State s;
        s.status = status;
        s.dest = dest;
        s.moved = moved;
        s.combatSinceSample = true;
        return s;
    };
    {
        State s = make(StatusTravel, Dest::Quest);
        s.parked = true;
        s.alive = false;
        Require(Classify(s) == Kind::Parked, "parked wins");
        s.parked = false;
        s.inCombat = true;
        Require(Classify(s) == Kind::Dead, "dead before combat");
        s.alive = true;
        Require(Classify(s) == Kind::Combat, "combat before the target");
        s.inCombat = false;
        s.helpFollower = true;
        Require(Classify(s) == Kind::Group, "a help-group follower is group, not quest or stuck (#627)");
        s.inCombat = true;
        Require(Classify(s) == Kind::Combat, "combat before group");
    }
    Require(Classify(make(StatusTravel, Dest::Quest)) == Kind::Quest, "travelling to a quest target");
    Require(Classify(make(StatusWork, Dest::Quest, 0.0f)) == Kind::Quest, "standing at a quest target in WORK is quest");
    Require(Classify(make(StatusTravel, Dest::Grind)) == Kind::Grind, "travelling to a grind target");
    Require(Classify(make(StatusWork, Dest::Grind, 0.0f)) == Kind::Grind, "grind WORK with a fight since the last sample");
    Require(Classify(make(StatusTravel, Dest::Other)) == Kind::Other, "travelling to another target");
    Require(Classify(make(2, Dest::Quest, 0.0f)) == Kind::Quest, "READY without moving is not stuck (only TRAVEL)");
    Require(Classify(make(StatusTravel, Dest::None)) == Kind::Idle, "active status without a destination is idle");
    Require(Classify(make(5, Dest::Quest)) == Kind::Idle, "quest target in cooldown is idle");
    Require(Classify(make(6, Dest::Grind)) == Kind::Idle, "expired grind target is idle");
    // OB-10 review: the L1-b stalls - TRAVEL without moving is stuck, a grind target without a fight is grind_idle
    Require(Classify(make(StatusTravel, Dest::Quest, 4.9f)) == Kind::Stuck, "Azora: TRAVEL to the taker, 4.9 yd moved");
    Require(Classify(make(StatusTravel, Dest::Quest, 5.0f)) == Kind::Quest, "5 yd moved is travelling");
    Require(Classify(make(StatusTravel, Dest::Grind, 0.0f)) == Kind::Stuck, "TRAVEL to a grind spot without moving");
    {
        State s = make(StatusWork, Dest::Grind, 30.0f);
        s.combatSinceSample = false;
        Require(Classify(s) == Kind::GrindIdle, "Fristan: grind WORK without any fight");
        s.status = StatusTravel;
        s.moved = 0.0f;
        Require(Classify(s) == Kind::Stuck, "stuck before grind_idle");
    }
    {
        State s = make(StatusTravel, Dest::Quest, 0.0f);
        s.taxiFlying = true;
        Require(Classify(s) == Kind::Quest, "on a taxi to a quest target: never stuck");
        s.status = 0;
        s.dest = Dest::None;
        Require(Classify(s) == Kind::Other, "taxi flight without target is other");
        s.taxiFlying = false;
        Require(Classify(s) == Kind::Idle, "nothing is idle");
    }
    // twow-repo#601 (OB-00 28.10.2026, CLI-PATH CAUSE.md): a RosterFarMove wait in TRAVEL is the target's kind, counted as far wait.
    {
        State s = make(StatusTravel, Dest::Quest, 0.0f);
        s.farMoveWaiting = true;
        Require(Classify(s) == Kind::Quest && FarWaitSample(s), "cand4-B: far-move wait to a quest target is quest, a far-wait sample");
        s.dest = Dest::Grind;
        Require(Classify(s) == Kind::Grind && FarWaitSample(s), "far-move wait to a grind spot is grind");
        s.dest = Dest::Other;
        Require(Classify(s) == Kind::Other && FarWaitSample(s), "far-move wait to another target is other");
        s.moved = 50.0f;
        Require(Classify(s) == Kind::Other && !FarWaitSample(s), "moving: no far-wait sample");
        s.moved = 0.0f;
        s.inCombat = true;
        Require(Classify(s) == Kind::Combat && !FarWaitSample(s), "combat stays combat, no far-wait sample");
        s.inCombat = false;
        s.status = StatusWork;
        s.dest = Dest::Grind;
        s.combatSinceSample = false;
        Require(Classify(s) == Kind::GrindIdle && !FarWaitSample(s), "WORK is never a far wait");
        s.status = 5;
        Require(Classify(s) == Kind::Idle && !FarWaitSample(s), "cooldown stays idle");
        s.status = StatusTravel;
        s.dest = Dest::Quest;
        s.farMoveWaiting = false;
        Require(Classify(s) == Kind::Stuck && !FarWaitSample(s), "no far move: stuck as before");
    }

    // Position and combat notes of the clock.
    {
        Clock c;
        Require(c.Move(0, 10.0f, 10.0f) > 1e8f, "no previous position: far");
        Require(c.Move(0, 13.0f, 14.0f) == 5.0f, "3-4-5: 5 yd");
        Require(c.Move(1, 13.0f, 14.0f) > 1e8f, "map change: far");
        Require(!c.TakeCombat(), "no combat noted");
        c.NoteCombat(false);
        c.NoteCombat(true);
        c.NoteCombat(false);
        Require(c.TakeCombat() && !c.TakeCombat(), "combat once in the interval, cleared by Take");
    }

    // Clock: the first sample credits nothing, later ones the seconds since the previous one; a long gap is gap.
    {
        Clock c;
        std::uint32_t const t0 = 3600u * 500000u + 100u;
        Require(c.Due(t0), "first sample due");
        Require(c.Add(Kind::Idle, t0) == 0 && c.hour == 500000u, "first sample credits nothing, sets the hour");
        Require(!c.Due(t0 + 59) && c.Due(t0 + 60), "next sample after 60 s");
        Require(c.Add(Kind::Quest, t0 + 60) == 60 && c.seconds[std::size_t(Kind::Quest)] == 60, "60 s to quest");
        Require(c.Add(Kind::Combat, t0 + 150) == 90 && c.seconds[std::size_t(Kind::Combat)] == 90, "90 s to combat");
        Require(c.Add(Kind::Idle, t0 + 271) == 0 && c.gap == 121 && c.seconds[std::size_t(Kind::Idle)] == 0, "121 s is a gap");
        Require(c.Add(Kind::Idle, t0 + 391) == 120 && c.seconds[std::size_t(Kind::Idle)] == 120, "120 s still credited");
        Require(!c.HourDone(t0 + 391), "same hour");
        // the next hour: Add first (seconds still go to the old hour), then the line and a fresh hour
        std::uint32_t const t1 = 3600u * 500001u + 10u;
        Require(c.Add(Kind::Grind, t1) == 0 && c.HourDone(t1), "long gap into the next hour, hour done");
        std::string const line = FormatLine(12, 14, 40, c);
        std::string const head = "[BotActivity] bot=12 level=14 zone=40 hour=500000 quest=60 grind=0 grind_idle=0 stuck=0 combat=90 "
            "other=0 idle=120 dead=0 parked=0 group=0 gap=3240";
        Require(line == head + " far_wait=0", "hour line");
        // twow-repo#601 quest acceptance: the quest log fields after gap=, left out without a count; far_wait= last.
        Require(FormatLine(12, 14, 40, c, 18, 3) == head + " quest_log=18 quest_done=3 far_wait=0", "hour line with the quest log");
        Require(FormatLine(12, 14, 40, c, 0, 0) == head + " quest_log=0 quest_done=0 far_wait=0", "an empty quest log is written");
        c.AddFarWait(60);
        c.AddFarWait(45);
        Require(FormatLine(12, 14, 40, c) == head + " far_wait=105", "far-move wait seconds of the hour");
        c.StartHour(t1);
        Require(c.hour == 500001u && c.gap == 0 && c.seconds[std::size_t(Kind::Quest)] == 0 && c.farWait == 0, "fresh hour");
        Require(c.Add(Kind::Grind, t1 + 60) == 60 && !c.HourDone(t1 + 60), "credit in the new hour");
    }

    // Counters and the minute line: samples and seconds per band, empty bands left out, Take resets.
    {
        Counters counters;
        counters.Add(BandOf(12), Kind::Quest, 60);
        counters.Add(BandOf(13), Kind::Idle, 60);
        counters.Add(BandOf(60), Kind::Dead, 30);
        counters.Add(Bands, Kind::Quest, 60);          // out of range: ignored
        counters.Add(0, Kind::Count, 60);               // no kind: ignored
        Snapshot const s = counters.Take();
        Require(s.samples[2] == 2 && s.seconds[2][std::size_t(Kind::Quest)] == 60 && s.seconds[2][std::size_t(Kind::Idle)] == 60,
            "band 10-14 counted");
        Require(FormatMinuteLine(s) == "[BotActivityMin] L10-14=2/60/0/0/0/0/0/60/0/0/0 L60+=1/0/0/0/0/0/0/0/30/0/0", "minute line");
        Require(FormatMinuteLine(counters.Take()) == "[BotActivityMin]", "Take resets");
    }

    // Many threads add, the totals match (relaxed atomics, no lock).
    {
        Counters counters;
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&counters] { for (int i = 0; i < 10000; ++i) counters.Add(3, Kind::Grind, 1); });
        for (std::thread& t : threads)
            t.join();
        Snapshot const s = counters.Take();
        Require(s.samples[3] == 80000 && s.seconds[3][std::size_t(Kind::Grind)] == 80000, "concurrent adds");
    }

    // twow-repo#601 StuckDetail: one line per episode, at the second stuck sample in a row; any other kind ends the episode.
    {
        StuckEpisode e;
        Require(!e.Note(true), "first stuck sample: no line yet");
        Require(e.Note(true), "second stuck sample in a row: the line");
        Require(!e.Note(true) && !e.Note(true), "the same episode: no second line");
        Require(!e.Note(false), "another kind ends the episode");
        Require(!e.Note(true) && e.Note(true), "a new episode: the line again");
        Require(!e.Note(false) && !e.Note(true) && !e.Note(false) && !e.Note(true), "single stuck samples: no line");
        StuckEpisode longOne;
        for (int i = 0; i < 300; ++i)
            Require(longOne.Note(true) == (i == 1), "a long episode logs once, the counter stops at 255");
    }

    // twow-repo#601 IdleDetail: why the target is not active.
    Require(IdleWhyOf(Dest::None, 0) == IdleWhy::NoTarget && IdleWhyOf(Dest::None, 5) == IdleWhy::NoTarget &&
        IdleWhyOf(Dest::None, 6) == IdleWhy::NoTarget, "an inactive status without a destination: no target");
    Require(IdleWhyOf(Dest::None, 3) == IdleWhy::NullDest && IdleWhyOf(Dest::None, 1) == IdleWhy::NullDest,
        "an active status without a destination");
    Require(IdleWhyOf(Dest::Quest, 0) == IdleWhy::None && IdleWhyOf(Dest::Quest, 5) == IdleWhy::Cooldown &&
        IdleWhyOf(Dest::Grind, 6) == IdleWhy::Expired, "none / cooldown / expired with a destination");
    Require(IdleWhyOf(Dest::Other, 7) == IdleWhy::Other, "an unknown status");
    Require(std::string(IdleWhyName(IdleWhy::NoTarget)) == "no_target" && std::string(IdleWhyName(IdleWhy::NullDest)) == "null_dest" &&
        std::string(IdleWhyName(IdleWhy::Cooldown)) == "cooldown" && std::string(IdleWhyName(IdleWhy::Expired)) == "expired" &&
        std::string(IdleWhyName(IdleWhy::None)) == "none" && std::string(IdleWhyName(IdleWhy::Other)) == "other", "names");
    Require(std::string(FetchStateName(false, false)) == "none" && std::string(FetchStateName(true, false)) == "pending" &&
        std::string(FetchStateName(true, true)) == "ready", "destination search state");

    // IdleDetail episode: the start at the second idle sample in a row (the reason then), the end at the next other sample with the
    // seconds since the first idle one; single idle samples write nothing.
    {
        IdleEpisode e;
        Require(e.Note(true, 1000, IdleWhy::Expired) == IdleNote::Nothing, "first idle sample: nothing yet");
        Require(e.Note(true, 1060, IdleWhy::Cooldown) == IdleNote::Start && e.why == IdleWhy::Cooldown, "second idle sample: start");
        Require(e.Note(true, 1120, IdleWhy::Expired) == IdleNote::Nothing && e.why == IdleWhy::Cooldown, "same episode: no second start");
        Require(e.Note(false, 1180, IdleWhy::NoTarget) == IdleNote::End && e.seconds == 180, "another kind: end, 180 s idle");
        Require(e.Note(false, 1240, IdleWhy::NoTarget) == IdleNote::Nothing, "no episode: nothing");
        Require(e.Note(true, 1300, IdleWhy::None) == IdleNote::Nothing && e.Note(false, 1360, IdleWhy::None) == IdleNote::Nothing,
            "a single idle sample: neither start nor end");
        IdleEpisode longOne;
        for (std::uint32_t i = 0; i < 300; ++i)
            Require(longOne.Note(true, 60 * i, IdleWhy::NoTarget) == (i == 1 ? IdleNote::Start : IdleNote::Nothing),
                "a long episode starts once, the counter stops at 255");
        Require(longOne.Note(false, 60 * 300, IdleWhy::NoTarget) == IdleNote::End && longOne.seconds == 60 * 300 && !longOne.endedByGap,
            "and ends once");
    }
    // IdleDetail episode across a sampling gap (a real master, no update): ends at the last idle sample, the next starts afresh.
    {
        IdleEpisode e;
        Require(e.Note(true, 1000, IdleWhy::Expired) == IdleNote::Nothing && e.Note(true, 1060, IdleWhy::Expired) == IdleNote::Start,
            "an episode");
        Require(e.Note(true, 1060 + 3 * 3600, IdleWhy::None) == IdleNote::End && e.seconds == 60 && e.endedByGap,
            "3 h without a sample: the episode ends at its last idle sample (60 s), not 3 h later");
        Require(e.Note(true, 1120 + 3 * 3600, IdleWhy::None) == IdleNote::Start && e.why == IdleWhy::None,
            "the idle sample after the gap counts as the first of a new episode");
        Require(e.Note(false, 1180 + 3 * 3600, IdleWhy::None) == IdleNote::End && e.seconds == 120 && !e.endedByGap, "which ends normally");
        IdleEpisode single;
        Require(single.Note(true, 0, IdleWhy::None) == IdleNote::Nothing && single.Note(false, 600, IdleWhy::None) == IdleNote::Nothing,
            "one idle sample before a gap: nothing");
        Require(single.Note(true, 660, IdleWhy::None) == IdleNote::Nothing && single.Note(true, 720, IdleWhy::None) == IdleNote::Start,
            "then a normal episode");
        Require(e.Note(true, 1240 + 3 * 3600, IdleWhy::None) == IdleNote::Nothing &&
            e.Note(true, 1240 + 3 * 3600 + MaxCreditSeconds, IdleWhy::None) == IdleNote::Start, "MaxCreditSeconds is no gap yet");
    }
    Require(&IdleDetailGlobal() != &StuckDetailGlobal(), "IdleDetail has its own budget");

    // StuckDetail limit: 30 lines a minute for all bots, the refused ones counted for the next line.
    {
        StuckDetailLimit limit;
        std::uint32_t const t0 = 493512u * 60u;
        for (std::uint32_t i = 0; i < StuckDetailPerMinute; ++i)
            Require(limit.Take(t0 + i % 60), "30 lines in the minute");
        Require(!limit.Take(t0 + 59) && !limit.Take(t0 + 30), "the 31st and 32nd line refused");
        Require(limit.TakeDropped() == 2 && limit.TakeDropped() == 0, "refused lines counted once");
        Require(limit.Take(t0 + 60), "the next minute starts afresh");
        Require(limit.Take(0), "minute 0 is a minute too");
    }

    // StuckDetail limit, many threads within a started minute: exactly 30 pass, the rest counted (only the minute change itself
    // may let a thread through once more - "about" in the policy).
    {
        StuckDetailLimit limit;
        Require(limit.Take(600), "the minute started");
        std::atomic<std::uint32_t> passed{ 1 };
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&limit, &passed] { for (int i = 0; i < 1000; ++i) if (limit.Take(600)) passed.fetch_add(1); });
        for (std::thread& t : threads)
            t.join();
        Require(passed.load() == StuckDetailPerMinute && limit.TakeDropped() == 8000 + 1 - StuckDetailPerMinute, "concurrent limit");
    }

    // The engine trail: the newest executed action and the trigger after its newest push (Engine::LogAction layout).
    {
        TrailPick p = PickFromTrail("|T:no target|PUSH:move to loot - 7.000000 (trigger)|T:far from current loot|PUSH:fish::travel - "
            "10.000000 (trigger)|T:can fish|PUSH:move to travel target - 1.000000 (trigger)|T:travel target traveling|"
            "TIME:values 0us triggers 276us|A:fish - USELESS 2us|A:move to loot - PREREQ 3us|M:mmap 13us pts=1|A:move to loot - OK 104us");
        Require(p.action == "move to loot" && p.trigger == "far from current loot", "cand3-C Thorra: move to loot, far from current loot");
        p = PickFromTrail("|PUSH:move to travel target - 1.000000 (trigger)|T:travel target traveling|A:move to travel target - OK");
        Require(p.action == "move to travel target" && p.trigger == "travel target traveling", "without the trace time");
        p = PickFromTrail("|PUSH:travel - -199.000000 (default)|A:travel - OK 3us");
        Require(p.action == "travel" && p.trigger == "-", "a default action has no trigger");
        p = PickFromTrail("p - 1.0 (trigger)|T:old|A:attack anything - USELESS 21us|no actions executed");
        Require(p.action == "-" && p.trigger == "-", "nothing executed in the trail");
        p = PickFromTrail("");
        Require(p.action == "-" && p.trigger == "-", "empty trail");
        p = PickFromTrail("|PUSH:a - 1.0 (trigger)|T:first|PUSH:a - 2.0 (trigger)|T:second|A:a - OK");
        Require(p.trigger == "second", "the newest push of the action");
        p = PickFromTrail("|A:say \"hi\" - OK");
        Require(p.action == "say 'hi'", "double quotes become single ones");
    }

    std::cout << "bot_activity_policy_tests passed\n";
    return 0;
}
