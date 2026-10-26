#include "playerbot/ReleaseVeto.h"

#include <atomic>

namespace ai::release_veto
{
namespace
{
std::atomic<Predicate> g_predicate{nullptr};
}

void Register(Predicate predicate)
{
    g_predicate.store(predicate);
}

bool Vetoed(Player* bot)
{
    Predicate const predicate = g_predicate.load();
    return bot && predicate && predicate(bot);
}
}
