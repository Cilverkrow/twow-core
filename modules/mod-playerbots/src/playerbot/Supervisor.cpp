#include "playerbot/playerbot.h"
#include "playerbot/Supervisor.h"

namespace ai::supervisor
{
Supervisor::Supervisor(PlayerbotAI* ai) : ai_(ai)
{
}

Supervisor::~Supervisor() = default;

// Scaffold (A3 step 3): no decision, no read or write of the bot. The behaviour pairs fill this tick in.
void Supervisor::Tick(std::uint32_t /*nowMs*/)
{
    ++ticks_;
}

// Scaffold: the supervisor owns nothing yet, so there is nothing to hand back.
void Supervisor::Release()
{
}
}
