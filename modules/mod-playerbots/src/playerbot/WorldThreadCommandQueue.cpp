#include "WorldThreadCommandQueue.h"

#include <algorithm>
#include <iterator>
#include <utility>

// No out-of-line definitions for the in-class initialised statics: C++17
// makes static constexpr data members implicitly inline, so they are already
// defined, and repeating them here would be a deprecated redeclaration.

std::future<std::string> WorldThreadCommandQueue::Post(std::string request)
{
    // The queue owns the promise. See the OWNERSHIP RULE in the header: the
    // caller holds only the future, so a caller that times out and destroys its
    // future leaves the promise alive and the drainer's set_value() safe.
    auto promise = std::make_shared<std::promise<std::string>>();
    std::future<std::string> pending = promise->get_future();

    bool accepted = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_pending.size() < MaxQueuedCommands)
        {
            m_pending.push_back(Entry{ std::move(request), promise });
            accepted = true;
        }
    }

    // Satisfied outside the lock, for the same reason the handler runs outside
    // it: no caller-visible work happens while a network thread could be
    // waiting on m_mutex.
    if (!accepted)
        promise->set_value(BusyReply);

    return pending;
}

void WorldThreadCommandQueue::Drain(std::size_t maxPerCall, std::function<std::string(std::string const&)> const& handler)
{
    if (!maxPerCall || !handler)
        return;

    // Everything under the lock is a splice: no handler, no allocation of
    // replies, no user code. The world thread holds m_mutex for as long as it
    // takes to move at most maxPerCall entries and no longer.
    std::deque<Entry> batch;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::size_t const count = std::min(maxPerCall, m_pending.size());
        if (!count)
            return;

        batch.insert(batch.end(),
            std::make_move_iterator(m_pending.begin()),
            std::make_move_iterator(m_pending.begin() + count));
        m_pending.erase(m_pending.begin(), m_pending.begin() + count);
    }

    for (Entry& entry : batch)
    {
        std::string reply;
        try
        {
            reply = handler(entry.request);
        }
        catch (...)
        {
            // The handler runs inside the world tick. An exception escaping
            // here would unwind the pump itself, so it is turned into a reply.
            reply = HandlerFailedReply;
        }

        try
        {
            entry.promise->set_value(std::move(reply));
        }
        catch (...)
        {
            // set_value() throws only on a promise with no shared state or one
            // already satisfied. Neither is reachable for an entry that made it
            // onto the queue -- a busy Post() never queues the entry it
            // satisfies -- but the world thread must not unwind on the way out
            // of a drain under any circumstances.
        }
    }
}

std::size_t WorldThreadCommandQueue::Size() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_pending.size();
}
