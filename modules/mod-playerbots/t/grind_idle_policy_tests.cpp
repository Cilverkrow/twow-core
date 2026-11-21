#include "GrindIdlePolicy.h"

#include <cstdlib>
#include <cstring>
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

ai::grind_idle::PassTally Pass(std::uint32_t at, std::uint16_t candidates)
{
    ai::grind_idle::PassTally t;
    t.Start(at);
    t.candidates = candidates;
    return t;
}

void Skips(ai::grind_idle::PassTally& t, ai::grind_idle::Skip why, int n)
{
    for (int i = 0; i < n; ++i)
        t.Note(why);
}
}

int main()
{
    using namespace ai::grind_idle;

    // No pass, a stale pass, a pass from the future (clock step): no_pass.
    PassTally none;
    Require(CauseOf(none, 1000, 0) == Cause::NoPass, "no pass yet");
    Require(CauseOf(Pass(1000, 3), 1000 + PassMaxAgeSeconds + 1, 0) == Cause::NoPass, "a pass older than 120 s");
    Require(CauseOf(Pass(1000, 0), 1000 + PassMaxAgeSeconds, 0) == Cause::Respawn, "120 s is still fresh");
    Require(CauseOf(Pass(1000, 3), 999, 0) == Cause::NoPass, "a pass after now");

    // A pick beats every refusal count.
    PassTally picked = Pass(1000, 4);
    Skips(picked, Skip::Grey, 3);
    picked.picked = true;
    Require(CauseOf(picked, 1010, 5) == Cause::Picked, "a pick");

    // No candidate: respawn below two players in sight, crowded from two.
    Require(CauseOf(Pass(1000, 0), 1010, 0) == Cause::Respawn, "empty spot, nobody around");
    Require(CauseOf(Pass(1000, 0), 1010, CrowdOthers - 1) == Cause::Respawn, "one other player is no crowd");
    Require(CauseOf(Pass(1000, 0), 1010, CrowdOthers) == Cause::Crowded, "empty spot with two players in sight");

    // Half the candidates (rounded up) decide; grey before tapped before the rules.
    PassTally grey = Pass(1000, 5);
    Skips(grey, Skip::Grey, 3);
    Skips(grey, Skip::Tapped, 2);
    Require(CauseOf(grey, 1010, 9) == Cause::Grey, "3 of 5 grey");
    PassTally greyLow = Pass(1000, 5);
    Skips(greyLow, Skip::Grey, 2);
    Skips(greyLow, Skip::Height, 3);
    Require(CauseOf(greyLow, 1010, 0) == Cause::Other, "2 of 5 grey is not half");
    PassTally tapped = Pass(1000, 4);
    Skips(tapped, Skip::Tapped, 2);
    Skips(tapped, Skip::Grey, 1);
    Require(CauseOf(tapped, 1010, 0) == Cause::Crowded, "2 of 4 tapped");
    PassTally rules = Pass(1000, 3);
    Skips(rules, Skip::Rule, 1);
    Skips(rules, Skip::NotNeeded, 1);
    Require(CauseOf(rules, 1010, 0) == Cause::Rules, "rule + not-needed 2 of 3");
    PassTally other = Pass(1000, 4);
    Skips(other, Skip::LevelAbove, 2);
    Skips(other, Skip::Elite, 2);
    Require(CauseOf(other, 1010, 0) == Cause::Other, "level and elite refusals");

    // Start clears the previous pass; the counters saturate.
    PassTally reused = Pass(1000, 2);
    Skips(reused, Skip::Grey, 2);
    reused.picked = true;
    reused.Start(2000);
    Require(reused.at == 2000 && !reused.candidates && !reused.picked && !reused.skips[std::uint8_t(Skip::Grey)], "Start resets");
    PassTally full = Pass(1000, 1);
    full.skips[std::uint8_t(Skip::Grey)] = 0xFFFF;
    full.Note(Skip::Grey);
    Require(full.skips[std::uint8_t(Skip::Grey)] == 0xFFFF, "saturates");

    // Grey spot: the template max level at or below the grey level; unknown template = no.
    Require(GreySpot(9, 9), "max level = grey level is grey");
    Require(!GreySpot(10, 9), "one above grey is not");
    Require(!GreySpot(0, 9), "unknown template");

    // Names (the log's vocabulary).
    Require(!std::strcmp(CauseName(Cause::NoPass), "no_pass") && !std::strcmp(CauseName(Cause::Respawn), "respawn") &&
        !std::strcmp(CauseName(Cause::Crowded), "crowded") && !std::strcmp(CauseName(Cause::Grey), "grey") &&
        !std::strcmp(CauseName(Cause::Rules), "rules") && !std::strcmp(CauseName(Cause::Picked), "picked") &&
        !std::strcmp(CauseName(Cause::Other), "other"), "cause names");

    std::cout << "grind idle policy tests passed\n";
    return 0;
}
