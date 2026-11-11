#include "OwnerContainerCommandPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#563 part 4: the chat commands that read or write the owner's containers.

namespace
{
using ai::owner_cmd::IsContainerReadingCommand;

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
    // the five container readers, with and without parameters, any case, leading blanks
    for (char const* text : { "rtsc", "rtsc move", "summon", "summon now", "invite", "invite Name", "join", "lfg", "lfg tank", "RTSC", "  Invite Name", "\tsummon" })
        Require(IsContainerReadingCommand(text), text);

    // commands that only read scalars of the owner keep running
    for (char const* text : { "follow", "stay", "attack", "attack my target", "cast frostbolt", "loot all", "co +dps", "nc +rpg", "reset", "wait 5", "logout",
                              "quests", "share", "accept", "talk", "trade", "t", "nt", "s", "e", "u", "gb", "who", "where", "formation near", "", " ", "do rtsc" })
        Require(!IsContainerReadingCommand(text), text);

    // only the first word counts, not a prefix of a longer word
    Require(!IsContainerReadingCommand("invitee"), "invitee is not invite");
    Require(!IsContainerReadingCommand("summoner"), "summoner is not summon");
    Require(!IsContainerReadingCommand("joinx"), "joinx is not join");
    Require(!IsContainerReadingCommand("x invite"), "invite is not the first word");

    std::cout << "owner container command policy tests passed\n";
    return 0;
}
