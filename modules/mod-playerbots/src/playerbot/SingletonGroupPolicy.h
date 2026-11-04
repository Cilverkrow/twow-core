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

// BOT-09 (AiPlayerbot.BotGroups.LeaderOtherMap, twow-repo#628 third finding): the ad-hoc group action
// (AdHoc.WorldThread = 0) carries out a leave its rules decided by asking the bot itself to leave, and the
// rule above kept every free bot - roster bots are free bots - so that leave never happened. adhocLeave =
// such a request with the switch on for the bot: it leaves through the same core path as any other bot.
// adhocLeave false = the rule above, unchanged.
inline bool ShouldStayInGroup(bool freeBot, bool inGroup, bool requestedByBotItself, bool selfLedSingleton, bool adhocLeave)
{
    return !adhocLeave && ShouldStayInGroup(freeBot, inGroup, requestedByBotItself, selfLedSingleton);
}
}
