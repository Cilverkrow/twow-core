#include "playerbot/playerbot.h"
#include "playerbot/Supervisor.h"

namespace ai::supervisor
{
Supervisor::Supervisor(PlayerbotAI* ai) : ai_(ai)
{
}

// Without Release (the bot's AI is torn down): the shadow state is dropped without a line and without touching the bot.
Supervisor::~Supervisor()
{
    ShadowDrop(shadow_);
}

// No decision and no write of the bot. Behaviour pair 1 (AiPlayerbot.Supervisor.Arbiter): the arbiter shadow closes the bundle
// of the engine pass before (SupervisorShadow.cpp); it only watches. shadowOn comes from the gate in PlayerbotAI::UpdateAI.
void Supervisor::Tick(std::uint32_t /*nowMs*/, bool shadowOn)
{
    ++ticks_;
    ShadowTick(ai_, shadow_, shadowOn);
}

// The supervisor owns nothing of the bot, so there is nothing to hand back; the arbiter shadow closes its open bundle.
void Supervisor::Release()
{
    ShadowRelease(ai_, shadow_);
}
}
