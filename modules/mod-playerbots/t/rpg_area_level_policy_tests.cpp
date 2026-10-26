// twow-repo#333 P1-v (RpgAreaLevelPolicy.h, AiPlayerbot.Travel.RpgAreaLevelFromSpawn): the area level of an RPG target from its
// spawn nearest to the bot instead of the map origin.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "RpgAreaLevelPolicy.h"

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
}

int main()
{
    using namespace ai::rpg_area_level;

    // 1) The rule stays the old one: more than 5 levels above the bot is not possible; unknown (0 or less) never refuses.
    {
        Require(!TooHighForBot(10, 5) && TooHighForBot(11, 5), "level 5: area 10 possible, 11 not");
        Require(!TooHighForBot(0, 5) && !TooHighForBot(-1, 5) && !TooHighForBot(-2, 1), "unknown area: never refused");
        Require(!TooHighForBot(30, 25) && TooHighForBot(31, 25), "the margin is 5 at every level");
        Require(kAboveBotLevels == 5, "margin as the quest-level gate");
    }

    // 2) The nearest spawn on the bot's map; none there: the first; nothing: npos.
    {
        Require(NearestSpawn({true, true, true}, {900.0f, 25.0f, 400.0f}) == 1, "the smallest squared distance");
        Require(NearestSpawn({false, true}, {1.0f, 50.0f}) == 1, "a spawn on another map does not count as near");
        Require(NearestSpawn({false, false}, {1.0f, 2.0f}) == 0, "no spawn on the bot's map: the first");
        Require(NearestSpawn({}, {}) == npos, "no spawn: npos");
        Require(NearestSpawn({true}, {}) == npos, "mismatched input: npos");
        Require(NearestSpawn({true, true}, {4.0f, 4.0f}) == 0, "a tie keeps the first");
    }

    // 3) The purpose names of the trip line (TravelValues.h bits).
    {
        Require(std::strcmp(PurposeName(1u << 9), "Vendor") == 0 && std::strcmp(PurposeName(1u << 8), "Repair") == 0, "sell / repair");
        Require(std::strcmp(PurposeName(1u << 7), "Trainer") == 0 && std::strcmp(PurposeName(1u << 6), "GenericRpg") == 0, "trainer / rpg");
        Require(std::strcmp(PurposeName(1u << 10), "AH") == 0 && std::strcmp(PurposeName(1u << 11), "Mail") == 0, "ah / mail");
        Require(std::strcmp(PurposeName(1u << 12), "other") == 0 && std::strcmp(PurposeName(0), "other") == 0, "anything else");
    }

    std::cout << "rpg_area_level_policy_tests: OK\n";
    return 0;
}
