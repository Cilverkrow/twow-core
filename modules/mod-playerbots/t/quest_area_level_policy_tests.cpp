#include "QuestAreaLevelPolicy.h"

#include <cstdlib>
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
    using ai::quest_area_level::IsQuestLocationLevelValid;

    // Live #335: level 1 night elves near Dolanaar (area level 5) got no quest point.
    Require(IsQuestLocationLevelValid(5, 1, 5), "level 1 bot may quest around Dolanaar");
    Require(IsQuestLocationLevelValid(6, 1, 5), "Starbreeze Village (6) is within the margin");
    Require(!IsQuestLocationLevelValid(9, 1, 5), "Oracle Glade (9) still waits at level 1");
    Require(IsQuestLocationLevelValid(9, 4, 5), "and opens at level 4");
    Require(IsQuestLocationLevelValid(0, 1, 5), "unknown area level is no reason to refuse");
    Require(IsQuestLocationLevelValid(-2, 1, 5), "missing area is no reason to refuse");
    Require(!IsQuestLocationLevelValid(20, 8, 5), "Hillsbrad stays closed to a level 8 bot");
    return 0;
}
