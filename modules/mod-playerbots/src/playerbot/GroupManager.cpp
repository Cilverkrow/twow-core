#include "playerbot/playerbot.h"
#include "playerbot/GroupManager.h"

namespace ai::group_mgr
{
// Scaffold (A3 step 6): no decision, no group read or write. The next pairs fill this pass in.
void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& /*bots*/, std::uint32_t /*now*/)
{
    ++passes_;
}

WorldThreadManager& WorldManager()
{
    static WorldThreadManager manager;
    return manager;
}
}
