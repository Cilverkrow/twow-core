#pragma once

class Player;
class Group;

namespace ai::bot_group
{
// twow-repo#365 step 1: one [BotGroup] line per join/leave of a free bot,
// behind AiPlayerbot.BotGroups.Diagnostics (default off). Read-only: it looks
// at the live group and the quest logs and changes nothing.
void LogMembership(Player* bot, Group* group, char const* event);
}
