// twow-repo#581 QL-3: few-spawn quest targets respawn faster per interested player, bounded both ways.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "../src/game/Objects/QuestTargetRespawnPolicy.h"

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
    using namespace quest_target_respawn;

    // Candidates: quest targets with 1..maxSpawns spawns.
    Require(IsCandidate(1, 3, true) && IsCandidate(3, 3, true), "1-3 spawns qualify");
    Require(!IsCandidate(4, 3, true), "negative: 4 spawns do not");
    Require(!IsCandidate(1, 3, false), "negative: no quest target");
    Require(!IsCandidate(0, 3, true), "negative: no spawn");

    // Annoying Peasant 50003 (260-400 s): more interested players, shorter respawn.
    Require(ScaledDelay(300, 0, 1.0f, 30) == 300, "nobody interested: unchanged");
    Require(ScaledDelay(300, 1, 1.0f, 30) == 150, "one: half");
    Require(ScaledDelay(300, 4, 1.0f, 30) == 60, "four: a fifth");
    Require(ScaledDelay(300, 20, 1.0f, 30) == 30, "many: floor MinSeconds");
    std::uint32_t last = 300;
    for (std::uint32_t n = 1; n <= 30; ++n)
    {
        std::uint32_t const d = ScaledDelay(300, n, 1.0f, 30);
        Require(d <= last && d >= 30, "monotonic and bounded");
        last = d;
    }

    // Never longer than before, nothing below the minimum, factor 0 = off.
    Require(ScaledDelay(20, 5, 1.0f, 30) == 20, "a delay already under MinSeconds stays");
    Require(ScaledDelay(300, 5, 0.0f, 30) == 300, "PerPlayer 0: off");
    Require(ScaledDelay(120, 1, 0.5f, 30) == 80, "PerPlayer 0.5: 120 / 1.5");

    std::cout << "quest_target_respawn_policy_test passed\n";
    return 0;
}
