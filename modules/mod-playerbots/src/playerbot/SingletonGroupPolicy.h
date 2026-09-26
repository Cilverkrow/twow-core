#pragma once

namespace ai::singleton_group
{
// twow-repo#301: a persistent roster bot can end up leader and sole member of
// its own group (e.g. an invite that was never answered). It then counts as
// grouped and cannot be invited, but a free bot's self-initiated leave was a
// no-op ("stay in the group"). Read from the live group, never from a caller.
inline bool IsSelfLedSingleton(unsigned int membersCount, bool botIsLeader, bool battlegroundGroup)
{
    return !battlegroundGroup && membersCount == 1 && botIsLeader;
}

// A free bot normally stays in its group when it asks itself to leave (it was
// invited there by someone). A self-led singleton is no group to stay in.
inline bool ShouldStayInGroup(bool freeBot, bool inGroup, bool requestedByBotItself, bool selfLedSingleton)
{
    return freeBot && inGroup && requestedByBotItself && !selfLedSingleton;
}
}
