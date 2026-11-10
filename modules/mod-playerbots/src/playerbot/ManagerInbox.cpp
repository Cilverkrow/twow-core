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

// T30-0: rare (a quest id >= 2^23), so it shares the inbox mutex instead of an atomic of its own.
std::uint64_t& KeyOverflows()
{
    static std::uint64_t count = 0;
    return count;
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

bool PostDirectiveLeft(std::uint32_t guidLow, std::uint16_t serial, std::uint8_t reason)
{
    Event event{};
    event.botLow = guidLow;
    event.kind = Kind::DirectiveAck;
    event.seq = 0;
    event.mapOrDungeon = std::uint32_t(kAckLeft) | (std::uint32_t(serial) << 16);
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

void CountKeyOverflow()
{
    std::lock_guard<std::mutex> lock(InboxMutex());
    ++KeyOverflows();
}

Counters Read()
{
    std::lock_guard<std::mutex> lock(InboxMutex());
    Counters counters = InboxRing().Read();
    counters.keyOverflow = KeyOverflows();
    return counters;
}

void LogMinute(MinuteLine const& minute)
{
    // The T30-0 line: 17 fields, ~200 chars of names plus at most 20 digits each - 640 never cuts it.
    char buf[640];
    FormatMinuteLine(buf, sizeof(buf), minute);
    sLog.outString("%s", buf);
}
}
