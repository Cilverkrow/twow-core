// A3 step 3 (ADR-0041 + amendment 2026-11-06, OB-00 06.11.2026; AiPlayerbot.Supervisor): the pure rules of the supervisor
// scaffold (SupervisorPolicy.h). The header names no game type, so this suite compiles it standalone. The game side (default 0,
// the one gated tick, nothing written by the supervisor) is pinned by
// t/contracts.d/a3s3_supervisor_scaffold_source_contract_tests.cmake.

#include "SupervisorPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
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

using namespace ai::supervisor;

Cadence const kCad = FullRate(60000, 10000);

bool Allowed(Mode from, Mode to, Requester who, std::uint32_t dwellMs = 3600000, Phase phase = Phase::Plan)
{
    return Decide(from, to, who, phase, dwellMs, kCad).allowed;
}

Deny Why(Mode from, Mode to, Requester who, std::uint32_t dwellMs = 3600000, Phase phase = Phase::Plan)
{
    return Decide(from, to, who, phase, dwellMs, kCad).reason;
}

void TestSwitch()
{
    Require(ValidMode(0) && ValidMode(1) && ValidMode(2) && ValidMode(3), "modes 0..3 are valid");
    Require(!ValidMode(4) && !ValidMode(99), "mode 4+ is not valid");
    Require(SanitizeMode(7) == 0, "an unknown mode is off");
    Require(!Applies(0, 1) && !Applies(0, 2), "mode 0 applies to nobody");
    Require(Applies(1, 1) && Applies(1, 2), "mode 1 applies to everybody");
    Require(Applies(2, 3) && !Applies(2, 4), "mode 2 = cohort B (odd low guid)");
    Require(Applies(3, 4) && !Applies(3, 3), "mode 3 = cohort A (even low guid)");
    Require(!Applies(5, 1) && !Applies(5, 2), "an unknown mode applies to nobody");
}

void TestModes()
{
    Require(kModes == 5, "exactly five modes");
    Require(IsMode(Mode::Quest) && IsMode(Mode::Grind) && IsMode(Mode::Rest) && IsMode(Mode::Group) && IsMode(Mode::Rescue),
        "the five modes are modes");
    Require(!IsMode(Mode::Passive), "PASSIVE is not a mode");
    Require(std::string(ModeName(Mode::Grind)) == "GRIND", "mode name");
    Require(std::string(ModeName(Mode::Passive)) == "PASSIVE", "passive name");
    Require(std::string(PhaseName(Phase::RegroupRest)) == "REGROUP_REST", "phase name");
    Require(std::string(RequesterName(Requester::LegacyWriter)) == "Legacy", "requester name");
    Require(std::string(DenyName(Deny::None)) == "-", "no deny reason");
}

void TestPriority()
{
    Require(Priority(Mode::Rescue) > Priority(Mode::Group), "RESCUE > GROUP");
    Require(Priority(Mode::Group) > Priority(Mode::Rest), "GROUP > REST");
    Require(Priority(Mode::Rest) > Priority(Mode::Quest), "REST > QUEST");
    Require(Priority(Mode::Quest) == Priority(Mode::Grind), "QUEST = GRIND");
    Require(Priority(Mode::Passive) > Priority(Mode::Rescue), "PASSIVE outranks everything");
}

void TestTransitionsQuestGrind()
{
    Require(Allowed(Mode::Quest, Mode::Grind, Requester::Planner), "planner: QUEST -> GRIND after the dwell time");
    Require(Why(Mode::Quest, Mode::Grind, Requester::Planner, 59999) == Deny::Hysteresis, "QUEST -> GRIND waits minDwellMs");
    Require(Allowed(Mode::Quest, Mode::Grind, Requester::Planner, 60000), "the dwell bound is inclusive");
    Require(Allowed(Mode::Grind, Mode::Quest, Requester::Planner, 0), "GRIND -> QUEST at once (QB4)");
    Require(Why(Mode::Quest, Mode::Grind, Requester::Self) == Deny::Requester, "QUEST <-> GRIND belongs to the planner");
    Require(Why(Mode::Quest, Mode::Grind, Requester::LegacyWriter) == Deny::Requester, "no legacy writer changes the mode");
    Require(Why(Mode::Quest, Mode::Quest, Requester::Planner) == Deny::SameMode, "same mode");
}

void TestTransitionsRest()
{
    Require(Allowed(Mode::Quest, Mode::Rest, Requester::RestGate), "RestGate: QUEST -> REST");
    Require(Allowed(Mode::Grind, Mode::Rest, Requester::Self), "Self: GRIND -> REST");
    Require(Why(Mode::Quest, Mode::Rest, Requester::Planner) == Deny::Requester, "the planner never goes to REST");
    Require(Why(Mode::Group, Mode::Rest, Requester::RestGate) == Deny::Requester, "no REST out of GROUP (RegroupRest instead)");
    Require(Why(Mode::Group, Mode::Rest, Requester::Self) == Deny::Requester, "not even Self rests out of GROUP");
    Require(Why(Mode::Group, Mode::Rest, Requester::GroupMgr) == Deny::Requester, "the manager leaves to QUEST/GRIND, not REST");
    Require(Allowed(Mode::Rest, Mode::Quest, Requester::Self), "Self: REST -> previous mode");
    Require(Why(Mode::Rest, Mode::Grind, Requester::Planner) == Deny::Requester, "the planner does not end a rest");
}

void TestTransitionsRescue()
{
    Mode const all[] = {Mode::Quest, Mode::Grind, Mode::Rest, Mode::Group};
    for (Mode from : all)
    {
        Require(Allowed(from, Mode::Rescue, Requester::Self, 0), "RESCUE from the event handler, at once");
        Require(Allowed(from, Mode::Rescue, Requester::CombatGuard, 0), "RESCUE from the R8 guard");
        Require(Allowed(from, Mode::Rescue, Requester::LegacyWriter, 0), "RESCUE from a legacy rescue writer is never refused");
        Require(Allowed(from, Mode::Rescue, Requester::ReplanSink, 0), "RESCUE from the R9 sink");
        Require(Why(from, Mode::Rescue, Requester::Planner, 0) == Deny::Requester, "the planner never goes to RESCUE");
    }
    Require(Allowed(Mode::Rescue, Mode::Quest, Requester::Self), "Self: RESCUE -> QUEST");
    Require(Allowed(Mode::Rescue, Mode::Group, Requester::Self), "Self: RESCUE -> GROUP (returnMode)");
    Require(Why(Mode::Rescue, Mode::Rest, Requester::Self) == Deny::Requester, "RESCUE does not end in REST");
    Require(Why(Mode::Rescue, Mode::Grind, Requester::Planner) == Deny::Requester, "only Self ends a rescue");
    Require(Why(Mode::Rescue, Mode::Group, Requester::GroupMgr) == Deny::RescueFirst, "GROUP is not entered out of RESCUE");
    Require(std::string(DenyName(Deny::RescueFirst)) == "rescue_first", "deny name");
    // RESCUE never depends on the phase: a stale RegroupRest neither refuses the funnel nor blocks the exit.
    Require(Allowed(Mode::Quest, Mode::Rescue, Requester::LegacyWriter, 0, Phase::RegroupRest), "funnel with a stale phase");
    Require(Allowed(Mode::Group, Mode::Rescue, Requester::Self, 0, Phase::RegroupRest), "RESCUE out of RegroupRest");
    Require(Allowed(Mode::Rescue, Mode::Group, Requester::Self, 0, Phase::RegroupRest), "RESCUE exit with a stale phase");
    Require(Allowed(Mode::Rescue, Mode::Quest, Requester::Self, 0, Phase::RegroupRest), "RESCUE exit to QUEST, stale phase");
}

void TestTransitionsGroup()
{
    Require(Allowed(Mode::Quest, Mode::Group, Requester::GroupMgr), "group manager: QUEST -> GROUP");
    Require(Allowed(Mode::Rest, Mode::Group, Requester::GroupMgr), "group manager: REST -> GROUP");
    Require(Why(Mode::Quest, Mode::Group, Requester::Planner) == Deny::Requester, "only the manager enters GROUP");
    Require(Why(Mode::Quest, Mode::Group, Requester::Self) == Deny::Requester, "not even Self enters GROUP");
    Require(Allowed(Mode::Group, Mode::Quest, Requester::GroupMgr), "manager: GROUP -> QUEST (leave/dissolve)");
    Require(Allowed(Mode::Group, Mode::Grind, Requester::Self), "Self: GROUP -> GRIND (group left)");
    Require(Why(Mode::Group, Mode::Quest, Requester::Planner) == Deny::Requester, "the planner does not leave a group");
    Require(Allowed(Mode::Group, Mode::Quest, Requester::GroupMgr, 0, Phase::RegroupRest), "RegroupRest is a GROUP phase");
    Require(Why(Mode::Quest, Mode::Grind, Requester::Planner, 3600000, Phase::RegroupRest) == Deny::Phase,
        "RegroupRest outside GROUP is refused");
}

void TestTransitionsPassive()
{
    Require(Allowed(Mode::Quest, Mode::Passive, Requester::External), "External: -> PASSIVE");
    Require(Allowed(Mode::Rescue, Mode::Passive, Requester::External), "PASSIVE outranks RESCUE");
    Require(Why(Mode::Quest, Mode::Passive, Requester::Self) == Deny::Requester, "only External enters PASSIVE");
    Require(Why(Mode::Passive, Mode::Rescue, Requester::Self) == Deny::PassiveHold, "PASSIVE holds still, even for RESCUE");
    Require(Why(Mode::Passive, Mode::Quest, Requester::Planner) == Deny::PassiveHold, "PASSIVE holds still for the planner");
    Require(Allowed(Mode::Passive, Mode::Quest, Requester::External), "end of the external lead: QUEST");
    Require(Allowed(Mode::Passive, Mode::Grind, Requester::External), "end of the external lead: GRIND");
    Require(Why(Mode::Passive, Mode::Group, Requester::External) == Deny::Requester, "External does not enter GROUP");
    Require(Why(Mode::Quest, Mode::Grind, Requester::External) == Deny::Requester, "External only toggles PASSIVE");
    Require(Why(Mode(9), Mode::Quest, Requester::Self) == Deny::NotAMode, "an out-of-range mode is refused");
    TransitionResult const r = Decide(Mode::Quest, Mode::Group, Requester::Planner, Phase::Plan, 0, kCad);
    Require(!r.allowed && r.to == Mode::Quest, "a refusal keeps the mode");
}

void TestCadence()
{
    Cadence c = FullRate(60000, 4000);
    Require(CadenceFloorMs(Mode::Quest, Phase::Travel, 100, c) == 100, "full rate: the react delay");
    c.ratePermille[std::size_t(Mode::Rest)][std::size_t(Phase::Plan)] = 250;
    Require(CadenceFloorMs(Mode::Rest, Phase::Plan, 100, c) == 400, "a quarter rate: x4");
    c.ratePermille[std::size_t(Mode::Quest)][std::size_t(Phase::Cooldown)] = 125;
    Require(CadenceFloorMs(Mode::Quest, Phase::Cooldown, 100, c) == 800, "an eighth: x8");
    Require(CadenceFloorMs(Mode::Quest, Phase::Cooldown, 1000, c) == 4000, "capped at maxDelayMs");
    c.maxDelayMs = 50;
    Require(CadenceFloorMs(Mode::Quest, Phase::Cooldown, 100, c) == 100, "never below the react delay");
    c.ratePermille[std::size_t(Mode::Grind)][std::size_t(Phase::Work)] = 0;
    Require(CadenceFloorMs(Mode::Grind, Phase::Work, 100, c) == 100, "an unset factor never throttles");
    c.ratePermille[std::size_t(Mode::Grind)][std::size_t(Phase::Work)] = 2000;
    Require(CadenceFloorMs(Mode::Grind, Phase::Work, 100, c) == 100, "a rate above 1000 never speeds up");
    Require(CadenceFloorMs(Mode::Passive, Phase::Plan, 100, c) == 100, "PASSIVE: the supervisor never slows the bot");
    Require(CadenceFloorMs(Mode::Rest, Phase(9), 100, c) == 100, "an unknown phase: the react delay");
    Cadence big = FullRate(0, 0xFFFFFFFFu);
    big.ratePermille[0][0] = 1;
    Require(CadenceFloorMs(Mode::Quest, Phase::Plan, 0xFFFFFFFFu, big) == 0xFFFFFFFFu, "no overflow at the top");
}

void TestFormatters()
{
    char buf[512];
    ModeLine m;
    m.guid = 42;
    m.name = "Bot\nName";
    m.cohort = 'B';
    m.arbiter = 1;
    m.level = 12;
    m.map = 1;
    m.zone = 14;
    m.cause = "Died";
    m.from = Mode::Quest;
    m.to = Mode::Rescue;
    m.req = Requester::Self;
    m.dwellMs = 1234;
    m.cadenceMs = 100;
    std::size_t const n = FormatModeLine(buf, sizeof(buf), m);
    std::string const line(buf);
    Require(n == line.size(), "the length is the written length");
    Require(line.rfind("[Supervisor] bot=42 name=Bot_Name cohort=B arbiter=1 ", 0) == 0, "tag first, newline replaced");
    Require(line.find(" ev=mode from=QUEST to=RESCUE phase=PLAN cause=Died req=Self dwell_ms=1234 cadence_ms=100 denied=0 deny_reason=-")
        != std::string::npos, "mode line fields");
    Require(line.find('\n') == std::string::npos && line.find('\r') == std::string::npos, "one line");

    m.deny = Deny::Hysteresis;
    FormatModeLine(buf, sizeof(buf), m);
    Require(std::string(buf).find("denied=1 deny_reason=hysteresis") != std::string::npos, "a refusal is visible");

    char small[16];
    std::size_t const cut = FormatModeLine(small, sizeof(small), m);
    Require(cut == 15 && std::strlen(small) == 15, "a short buffer is cut and terminated");
    Require(FormatModeLine(small, 0, m) == 0, "a zero buffer writes nothing");

    std::string longName(200, 'x');
    m.name = longName.c_str();
    FormatModeLine(buf, sizeof(buf), m);
    Require(std::string(buf).find(" name=" + std::string(40, 'x') + " cohort=") != std::string::npos, "the name is cut at 40");

    PlanLine p;
    p.guid = 7;
    p.cohort = 'A';
    p.mode = Mode::Quest;
    p.kind = "quest_turnin";
    p.id = 783;
    p.destId = 5;
    p.value = 12.5f;
    p.xpm = 30.4f;
    p.routeSec = 60;
    p.turnIn = true;
    p.reason = "turnin_first";
    p.cands = 4;
    p.alt[0].kind = "grind";
    p.alt[0].id = 3;
    p.alt[0].value = 4.5f;
    std::size_t const pn = FormatPlanLine(buf, sizeof(buf), p);
    std::string const plan(buf);
    Require(pn == plan.size(), "plan line length");
    Require(plan.rfind("[Plan] bot=7 cohort=A arbiter=0 mode=QUEST pick=quest_turnin:783:5 value=12.50 xpm=30.4 route_s=60 ", 0) == 0,
        "plan line head");
    Require(plan.find(" turnin=1 brain=0 reason=turnin_first cands=4 alt1=grind:3:4.50 alt2=- alt3=- blocked=0 budget_ms=0")
        != std::string::npos, "plan line tail");
}
}

int main()
{
    TestSwitch();
    TestModes();
    TestPriority();
    TestTransitionsQuestGrind();
    TestTransitionsRest();
    TestTransitionsRescue();
    TestTransitionsGroup();
    TestTransitionsPassive();
    TestCadence();
    TestFormatters();
    std::cout << "supervisor_policy_tests: OK\n";
    return 0;
}
