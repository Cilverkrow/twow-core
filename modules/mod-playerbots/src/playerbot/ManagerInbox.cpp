#include "playerbot/playerbot.h"
#include "playerbot/ManagerInbox.h"

#include <mutex>

namespace ai::manager_inbox
{
namespace
{
// Function-local statics: made once on first use, thread-safe since C++11, no static-init order issue.
std::mutex& InboxMutex()
{
    static std::mutex mutex;
    return mutex;
}

Ring<Capacity>& InboxRing()
{
    static Ring<Capacity> ring;
    return ring;
}
}

bool Post(Event const& event)
{
    std::lock_guard<std::mutex> lock(InboxMutex());
    return InboxRing().Push(event);
}

bool PostDirectiveAck(std::uint32_t guidLow, std::uint32_t seq, std::uint8_t state, std::uint8_t reason)
{
    Event event{};
    event.botLow = guidLow;
    event.kind = Kind::DirectiveAck;
    event.seq = seq;
    event.mapOrDungeon = state;
    event.reason = reason;
    return Post(event);
}

std::size_t Drain(std::vector<Event>& out, std::size_t max)
{
    // Grow the vector before taking the lock, so the lock only covers the copy.
    out.reserve(out.size() + max);
    std::lock_guard<std::mutex> lock(InboxMutex());
    return InboxRing().Drain(out, max);
}

Counters Read()
{
    std::lock_guard<std::mutex> lock(InboxMutex());
    return InboxRing().Read();
}

void LogMinute(MinuteLine const& minute)
{
    char buf[256];
    FormatMinuteLine(buf, sizeof(buf), minute);
    sLog.outString("%s", buf);
}
}
