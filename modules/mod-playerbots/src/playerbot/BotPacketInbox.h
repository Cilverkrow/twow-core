#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace ai
{
// Server-wide counters for the [BotInbox] minute line: packets dropped past the bound, and the
// largest batch one drain handed over. Reset by the reader.
inline std::atomic<std::uint64_t>& InboxDroppedTotal()
{
    static std::atomic<std::uint64_t> dropped{0};
    return dropped;
}

inline std::atomic<std::uint64_t>& InboxLargestDrain()
{
    static std::atomic<std::uint64_t> largest{0};
    return largest;
}

// twow-repo#563 (X1): packets sent to a bot's session arrive on the sender's thread (a world
// channel message reaches every bot from the talking bot's region thread). They are only queued
// here, under a lock; the bot drains the queue at the start of its own update and handles them on
// its own thread. Bounded: past Capacity the oldest packet is dropped and counted (a bot that does
// not update, e.g. during a far teleport, must not collect world chat without limit).
template <class T>
class BoundedInbox
{
public:
    explicit BoundedInbox(std::size_t capacity) : capacity(capacity ? capacity : 1) {}

    void Push(T item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.size() >= capacity)
        {
            queue.pop_front();
            ++dropped;
            InboxDroppedTotal().fetch_add(1, std::memory_order_relaxed);
        }
        queue.push_back(std::move(item));
    }

    // Moves every queued item into out (in arrival order) and leaves the inbox empty.
    void Drain(std::vector<T>& out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::uint64_t const batch = queue.size();
        std::uint64_t seen = InboxLargestDrain().load(std::memory_order_relaxed);
        while (batch > seen && !InboxLargestDrain().compare_exchange_weak(seen, batch, std::memory_order_relaxed))
        {
        }
        out.reserve(out.size() + queue.size());
        for (T& item : queue)
            out.push_back(std::move(item));
        queue.clear();
    }

    std::size_t Size()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }

    std::uint64_t Dropped()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return dropped;
    }

private:
    std::size_t const capacity;
    std::mutex mutex;
    std::deque<T> queue;
    std::uint64_t dropped = 0;
};

// Enough for a busy world channel between two updates of a bot; packets past it are old chat.
constexpr std::size_t BotPacketInboxCapacity = 256;
}
