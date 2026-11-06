#pragma once

#include "playerbot/GroupManagerPolicy.h"

#include <cstdint>
#include <map>

class Player;

// A3 step 6 (ADR-0041, OB-00 06.11.2026; AiPlayerbot.BotGroups.Manager, default 0): the one owner of the groups of roster
// bots, on the world thread (RandomPlayerbotMgr::UpdateAIInternal, maps idle, next to S7 and #628). SCAFFOLD: the pass only
// counts itself. Forming, changing and leaving groups, the meeting point and the dungeon episodes (G-1..G-3) come in their own
// pairs, each behind this switch and each with its own A/B (ADR-0041 rules 1 and 3). Every membership change will go through
// the world-thread op path of BOT-12 (GroupOps.h); nothing in here may call a group handler or Group::* directly.
namespace ai::group_mgr
{
class WorldThreadManager
{
public:
    void Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);

    std::uint64_t Passes() const { return passes_; }

private:
    std::uint64_t passes_ = 0;
};

WorldThreadManager& WorldManager();
}
