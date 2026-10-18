// twow-repo#601: the 8.32 grind fallback holds only GrindFallbackMinutes after the last empty quest route search.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "QuestSearchPolicy.h"

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
    using namespace ai::quest_search;
    std::uint32_t const failedAt = 1000000;

    // A L16 bot in the Barrens: two empty searches, so the 8.32 fallback grinds (GrindFallbackFailures 2).
    Require(GrindFallback(true, 2, 2), "8.32 fallback after 2 failures");
    Require(GrindFallbackFresh(10, failedAt, failedAt + 9 * 60), "10 min: still grinding after 9 min");
    Require(!GrindFallbackFresh(10, failedAt, failedAt + 10 * 60), "10 min: the quest request may fire again");
    Require(!GrindFallbackFresh(10, failedAt, failedAt + 3 * 3600), "hours later: no endless grind");

    // Negative: 0 = as before (for good); no stamp (failures from before the switch) = as before.
    Require(GrindFallbackFresh(0, failedAt, failedAt + 24 * 3600), "0: the fallback holds for good");
    Require(GrindFallbackFresh(10, 0, failedAt), "no stamp: as before");
    Require(GrindFallbackFresh(10, failedAt + 5, failedAt), "clock before the stamp: no early end");

    std::cout << "grind_fallback_minutes_tests passed\n";
    return 0;
}
