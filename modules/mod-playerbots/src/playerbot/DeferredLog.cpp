#include "playerbot/DeferredLog.h"

namespace ai::deferred_log
{
namespace
{
thread_local PlayerbotAI* t_target = nullptr;
}

PlayerbotAI* Target()
{
    return t_target;
}

Scope::Scope(PlayerbotAI* ai, bool on)
{
    if (!on || !ai)
        return;
    previous = t_target;
    t_target = ai;
    active = true;
}

Scope::~Scope()
{
    if (active)
        t_target = previous;
}
}
