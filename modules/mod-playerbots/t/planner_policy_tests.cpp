// A3 step 3 (ADR-0041 + amendment 2026-11-06, OB-00 06.11.2026; AiPlayerbot.Supervisor): the pure rules of the activity
// planner scaffold (PlannerPolicy.h). The header names no game type, so this suite compiles it standalone. Nothing in the game
// calls the planner yet; t/contracts.d/a3s3_supervisor_scaffold_source_contract_tests.cmake pins the pure header.

#include "PlannerPolicy.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
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

bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-3f;
}

using namespace ai::planner;

Candidate Make(Kind kind, std::uint32_t id, float xp, float routeSec, float workSec)
{
    Candidate c;
    c.kind = kind;
    c.questId = id;
    c.destId = id;
    c.xp = xp;
    c.routeSec = routeSec;
    c.workSec = workSec;
    return c;
}

void TestXpPerMin()
{
    Weights w;
    Candidate c = Make(Kind::QuestObjective, 1, 600.0f, 60.0f, 120.0f);
    c.restSec = 60.0f;
    Require(Near(XpPerMin(c, w), 150.0f), "600 xp over 4 minutes = 150 per minute");
    c.followUpXp = 600.0f;
    Require(Near(XpPerMin(c, w), 300.0f), "the follow-up quest counts");
    Candidate zero = Make(Kind::QuestObjective, 2, 100.0f, 0.0f, 0.0f);
    Require(Near(XpPerMin(zero, w), 6000.0f), "a zero route gives a finite rate (1 s floor)");
    Candidate neg = Make(Kind::QuestObjective, 3, -50.0f, -10.0f, 60.0f);
    Require(Near(XpPerMin(neg, w), 0.0f), "negative inputs count as 0");
    Require(std::string(KindName(Kind::QuestTurnIn)) == "quest_turnin", "kind name");
    Require(std::string(ReasonName(Reason::TurnInFirst)) == "turnin_first", "reason name");
}

void TestTurnInFirst()
{
    Weights w;
    w.turnInWorkSec = 5.0f;
    // the same xp and route: the turn-in has (almost) no work, so it wins without any boost
    Candidate in[2] = {Make(Kind::QuestObjective, 10, 900.0f, 120.0f, 300.0f), Make(Kind::QuestTurnIn, 11, 900.0f, 120.0f, 300.0f)};
    Require(Near(WorkSec(in[1], w), 5.0f), "a turn-in's work time is turnInWorkSec");
    Scored out[2];
    std::size_t const n = Rank(in, 2, w, out, 2);
    Require(n == 2, "both ranked");
    Require(out[0].c == &in[1], "the turn-in beats an equal objective");
    Require(out[0].reason == Reason::TurnInFirst && out[1].reason == Reason::Alternative, "reason turnin_first");
}

void TestDanger()
{
    Weights w;
    w.dangerPenalty = 0.5f;
    Candidate c = Make(Kind::Grind, 1, 600.0f, 0.0f, 60.0f);
    float const safe = Value(c, w);
    c.danger = 1.0f;
    Require(Near(Value(c, w), safe * 0.5f), "full danger costs dangerPenalty of the value");
    c.danger = 7.0f;
    Require(Near(Value(c, w), safe * 0.5f), "danger is clamped to 1");
    w.dangerPenalty = 3.0f;
    Require(Near(Value(c, w), 0.0f), "the penalty is clamped to 1");
    c.danger = -1.0f;
    Require(Near(Value(c, w), safe), "negative danger is 0");
}

void TestSwitchCostAndBrain()
{
    Weights w;
    w.switchCost = 20.0f;
    w.brainBonusMax = 10.0f;
    Candidate here = Make(Kind::QuestObjective, 1, 600.0f, 0.0f, 60.0f);
    Candidate there = here;
    there.zoneChange = true;
    Require(Near(Value(here, w) - Value(there, w), 20.0f), "a zone change costs switchCost");
    Candidate liked = here;
    liked.brainPref = 255;
    Require(Near(Value(liked, w) - Value(here, w), 10.0f), "brainPref 255 = brainBonusMax");
    liked.brainPref = 51;
    Require(Near(Value(liked, w) - Value(here, w), 2.0f), "the bonus scales with brainPref");
    w.brainBonusMax = -5.0f;
    Require(Near(Value(liked, w), Value(here, w)), "a negative cap gives no bonus");
}

void TestRankOrder()
{
    Weights w;
    Candidate in[5] = {
        Make(Kind::QuestObjective, 1, 100.0f, 0.0f, 60.0f),   // 100/min
        Make(Kind::QuestObjective, 2, 300.0f, 0.0f, 60.0f),   // 300/min
        Make(Kind::Grind, 3, 300.0f, 0.0f, 60.0f),            // 300/min, after #2 (stable)
        Make(Kind::QuestObjective, 4, 900.0f, 0.0f, 60.0f),   // blocked
        Make(Kind::Errand, 5, 200.0f, 0.0f, 60.0f),           // 200/min
    };
    in[3].blockedReason = 1;
    Scored out[3];
    std::size_t const n = Rank(in, 5, w, out, 3);
    Require(n == 3, "top 3 written");
    Require(out[0].c == &in[1] && out[1].c == &in[2] && out[2].c == &in[4], "by value, ties in input order, blocked left out");
    Require(out[0].reason == Reason::BestValue, "reason best_value");
    Require(Near(out[0].xpPerMin, 300.0f) && Near(out[0].value, 300.0f), "scored fields");

    Scored again[3];
    Rank(in, 5, w, again, 3);
    for (std::size_t i = 0; i < 3; ++i)
        Require(again[i].c == out[i].c, "deterministic");

    Scored one[1];
    Require(Rank(in, 5, w, one, 1) == 1 && one[0].c == &in[1], "k = 1 keeps the best");
    Require(Rank(in, 5, w, one, 0) == 0, "k = 0 writes nothing");
    Require(Rank(nullptr, 5, w, one, 1) == 0, "no input");
    Require(Rank(in, 0, w, one, 1) == 0, "an empty list");

    Candidate solo[2] = {Make(Kind::Grind, 8, 50.0f, 0.0f, 60.0f), Make(Kind::QuestObjective, 9, 900.0f, 0.0f, 60.0f)};
    solo[1].blockedReason = 3;
    Scored s[2];
    Require(Rank(solo, 2, w, s, 2) == 1 && s[0].reason == Reason::OnlyCandidate, "only one eligible: only_candidate");

    Candidate inf[2] = {Make(Kind::Grind, 1, std::numeric_limits<float>::infinity(), 0.0f, 60.0f),
        Make(Kind::Grind, 2, 60.0f, 0.0f, 60.0f)};
    Weights full;
    full.dangerPenalty = 1.0f;
    inf[0].danger = 1.0f;   // inf x 0 = NaN
    Require(Rank(inf, 2, full, s, 2) == 1 && s[0].c == &inf[1], "a NaN value is not eligible");
}

void TestRankKeepsBestAcrossLongLists()
{
    Weights w;
    Candidate in[12];
    for (std::size_t i = 0; i < 12; ++i)
        in[i] = Make(Kind::QuestObjective, std::uint32_t(i), float((i * 7) % 12) * 60.0f, 0.0f, 60.0f);
    Scored out[3];
    Require(Rank(in, 12, w, out, 3) == 3, "three of twelve");
    Require(out[0].value >= out[1].value && out[1].value >= out[2].value, "sorted");
    Require(Near(out[0].value, 660.0f) && Near(out[1].value, 600.0f) && Near(out[2].value, 540.0f), "the three best of twelve");
}
}

int main()
{
    TestXpPerMin();
    TestTurnInFirst();
    TestDanger();
    TestSwitchCostAndBrain();
    TestRankOrder();
    TestRankKeepsBestAcrossLongLists();
    std::cout << "planner_policy_tests: OK\n";
    return 0;
}
