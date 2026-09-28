#include "GatherNodePolicy.h"

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
    using namespace ai::gather_node;

    // A herb or ore node: chest type, lock with a profession skill only.
    Require(IsGatherNode(true, false), "a herb/ore node is gathered");

    // #414: Beached Sea Creature / Turtle - quest-giver type (2), lock 259 with
    // "open kneeling" next to herbalism.
    Require(!IsGatherNode(false, true), "beached quest objects (type 2, lock 259) are no herbs");
    // 175207 and the quest plants (Serpentbloom ...) are chests with lock 259.
    Require(!IsGatherNode(true, true), "a chest whose lock also asks open kneeling is no herb");
    Require(!IsGatherNode(false, false), "only chest-type objects are gathered");

    // Gathering stays on the bot's map; other purposes may cross maps.
    Require(StaysOnMap(true, 0, 0), "a herb on the own map is fine");
    Require(!StaysOnMap(true, 0, 1), "#414: no herb trip from Tirisfal (0) to Darkshore (1)");
    Require(StaysOnMap(false, 0, 1), "quest travel may still cross maps");

    std::cout << "gather_node_policy_tests passed\n";
    return 0;
}
