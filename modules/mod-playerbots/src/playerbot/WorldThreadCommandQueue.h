#ifndef _WorldThreadCommandQueue_H
#define _WorldThreadCommandQueue_H

// A hand-off point between an arbitrary thread and the world thread.
//
// The world is single-threaded. Everything reachable from a Player* -- the AI,
// its context, the object accessor tables -- is owned by the world thread and
// may be destroyed by it at any tick. A network thread that materialises a
// Player* and dereferences it has a use-after-free the moment the world thread
// despawns that object on the same tick, and no amount of null-checking on the
// network side closes the window: the pointer is valid when it is read and
// dangling when it is used.
//
// So nothing crosses this boundary but bytes. A caller Post()s a request
// string and gets a future; the world thread Drain()s the queue inside its own
// tick, runs the handler there, and answers with a reply string.
//
// DELIBERATELY NOT A std::function<void()> QUEUE. A closure carries an
// arbitrary capture list, so the instant a generic PostToWorldThread(fn)
// exists, `[bot]{ bot->Foo(); }` compiles -- and the rule this class exists to
// enforce becomes unenforceable at precisely the boundary it guards. The
// element type is std::string in both directions and there is no way to smuggle
// a pointer across it.
//
// The handler is injected at drain time rather than stored, which is why this
// header (and its .cpp) name no game header and no playerbot header. That is a
// hard design constraint, not an accident: it is what lets the unit suite in
// t/world_thread_command_queue_tests.cpp compile this pair of translation units
// standalone, with no world, no database and no bot tree behind them.
//
// OWNERSHIP RULE. The queue owns each promise through a shared_ptr, and that
// shared_ptr outlives the waiter by construction. A waiter that times out drops
// its std::future and walks away; the promise stays alive in the queue entry,
// and the world thread later set_value()s into a live object whose result
// nobody reads. Nothing is ever destroyed under another thread, and the world
// thread never touches a waiter's storage. The reverse arrangement -- the
// waiter owning the promise -- is the bug this class is here to avoid: it puts
// a destructor on the network thread racing a set_value() on the world thread.

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

class WorldThreadCommandQueue
{
    public:
        // Backpressure. Past this many unanswered requests Post() refuses
        // rather than queueing, because the world thread drains at a bounded
        // rate and an unbounded queue turns a stuck or slow tick into
        // unbounded memory growth driven by a remote peer. 256 is far more
        // than the handful of admin connections this server ever carries, so
        // reaching it means something is already wrong and the right answer is
        // to say so immediately.
        static constexpr std::size_t MaxQueuedCommands = 256;

        // How much of the queue one tick is allowed to run. The handler runs
        // inside the world tick, so an unbounded drain would let a burst of
        // remote commands stall the whole server for as long as the burst
        // lasts. 64 per tick empties a full queue in four ticks while keeping
        // any single tick's extra work bounded.
        static constexpr std::size_t MaxCommandsPerTick = 64;

        // How long a waiter blocks before giving up. This is the constant that
        // matters most, and it is not about slow commands: it covers world
        // thread shutdown, where nothing will ever drain the queue again. With
        // no timeout every connection thread would park forever on a promise
        // nobody is left to fulfil. Five seconds is far longer than any handler
        // needs and short enough that a dead world is reported, not hung on.
        static constexpr std::chrono::seconds WaiterTimeout{5};

        // The reply Post() hands back, already satisfied, when the queue is
        // full. A word rather than an error code because the wire format here
        // is one line of text.
        static constexpr char const* BusyReply = "busy";

        // The reply substituted when the handler throws. The handler runs on
        // the world thread inside the tick; letting an exception escape Drain()
        // would take the pump with it.
        static constexpr char const* HandlerFailedReply = "error";

        WorldThreadCommandQueue() = default;

        WorldThreadCommandQueue(WorldThreadCommandQueue const&) = delete;
        WorldThreadCommandQueue& operator=(WorldThreadCommandQueue const&) = delete;

        // Callable from ANY thread. Returns immediately; the returned future is
        // satisfied by whichever thread later calls Drain(). If the queue is at
        // capacity the future is already satisfied with BusyReply.
        std::future<std::string> Post(std::string request);

        // WORLD THREAD ONLY. Takes up to maxPerCall entries off the queue under
        // the mutex, then runs `handler` on each of them OUTSIDE the lock -- the
        // handler reaches into the world and must never do so while holding a
        // lock a network thread can block on. Each promise is satisfied with the
        // handler's reply.
        void Drain(std::size_t maxPerCall, std::function<std::string(std::string const&)> const& handler);

        // Diagnostics and tests. The value is stale the moment it is returned
        // if any other thread is posting.
        std::size_t Size() const;

    private:
        struct Entry
        {
            std::string request;
            std::shared_ptr<std::promise<std::string>> promise;
        };

        mutable std::mutex m_mutex;
        std::deque<Entry> m_pending;
};

#endif
