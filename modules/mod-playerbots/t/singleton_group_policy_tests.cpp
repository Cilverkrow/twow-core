#include "SingletonGroupPolicy.h"

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
    using namespace ai::singleton_group;

    Require(IsSelfLedSingleton(1, true, false), "leader and sole member is the stranded state");
    Require(!IsSelfLedSingleton(2, true, false), "a leader with a member is a real group");
    Require(!IsSelfLedSingleton(5, true, false), "a leader with a party is a real group");
    Require(!IsSelfLedSingleton(1, false, false), "a group led by someone else is not the bot's to disband");
    Require(!IsSelfLedSingleton(1, true, true), "battleground groups are left alone");
    Require(!IsSelfLedSingleton(0, true, false), "no members is not the stranded state");

    // The old rule: a free bot asking itself to leave stays in its group.
    Require(ShouldStayInGroup(true, true, true, false), "a free bot keeps a real group it was invited into");
    // #301: not when that group is only itself.
    Require(!ShouldStayInGroup(true, true, true, true), "a self-led singleton is left even on the bot's own request");
    Require(!ShouldStayInGroup(true, true, false, false), "a player's leave request is carried out");
    Require(!ShouldStayInGroup(false, true, true, false), "a bot with a master leaves when told");
    Require(!ShouldStayInGroup(true, false, true, false), "not in a group: nothing to stay in (repeat is a no-op)");
    return 0;
}
