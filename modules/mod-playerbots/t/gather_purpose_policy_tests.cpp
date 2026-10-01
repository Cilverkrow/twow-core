#include "GatherPurposePolicy.h"

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
}

int main()
{
    using namespace ai::gather_purpose;

    State s;
    Require(Decide(s, true, 393, 1, 75, 1000) == Decision::Start, "skill behind, no purpose: start one");
    Require(Decide(s, false, 393, 1, 75, 1000) == Decision::Yes, "a bot with a real player gathers as before");
    Require(Decide(s, true, 393, 75, 75, 1000) == Decision::No, "skill at its target: no purpose");

    s.Start(393, 1, 75, 1000);
    Require(Decide(s, true, 393, 1, 75, 1001) == Decision::Yes, "the declared purpose continues");
    Require(Decide(s, true, 182, 1, 75, 1001) == Decision::No, "one declared purpose at a time");

    // Latchigedap: skinning 1/75 for hours without a skill-up.
    Require(s.Observe(1, 1000 + NoSkillupSeconds - 1) == End::None, "under 15 minutes without a skill-up");
    Require(s.Observe(1, 1000 + NoSkillupSeconds) == End::NoSkillup, "15 minutes without a skill-up end it");
    Require(!s.Active() && s.Blocked(393, 1000 + NoSkillupSeconds + 1), "then skinning is blocked");
    Require(Decide(s, true, 393, 1, 75, 1000 + NoSkillupSeconds + 60) == Decision::No, "no new skinning purpose while blocked");
    Require(!s.Blocked(393, 1000 + NoSkillupSeconds + BlockSeconds), "blocked for one hour");
    Require(!s.Blocked(182, 1000 + NoSkillupSeconds + 1), "only that profession");

    State b;
    b.Start(186, 10, 75, 0);
    for (uint32_t t = 60, v = 11; t < BudgetSeconds; t += 60, ++v)
        Require(b.Observe(v, t) == End::None, "skill-ups keep the purpose running");
    Require(b.Observe(40, BudgetSeconds) == End::Budget, "the budget ends it");
    Require(b.Blocked(186, BudgetSeconds + 1), "a spent budget blocks too");

    State r;
    r.Start(356, 70, 75, 0);
    Require(r.Observe(75, 300) == End::TargetReached, "target reached");
    Require(!r.Active() && !r.Blocked(356, 301), "a reached target blocks nothing");

    Require(std::string(ProfessionName(393)) == "skinning" && std::string(EndName(End::NoSkillup)) == "no_skillup", "log names");

    std::cout << "gather_purpose_policy_tests passed\n";
    return 0;
}
