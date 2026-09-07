// Unit suite for WorldThreadCommandQueue -- the hand-off that keeps
// PlayerbotCommandServer connection threads out of the world's object graph.
//
// The bug this queue exists for is twow-repo issue #202: session() ran
// RandomPlayerbotMgr::HandleRemoteCommand on a detached network thread, and
// that function resolves a guid to a Player* and a PlayerbotAI* and
// dereferences both. Despawn the bot on the same tick and the pointer is valid
// when it is read and dangling when it is used. The fix is that nothing but
// bytes crosses the thread boundary any more.
//
// Only the queue is testable here, and deliberately so: it names no game header
// and no playerbot header, so this suite compiles it as a standalone pair of
// translation units with no world, no database and no bot tree behind it. The
// half that needs a world -- that the handler really does run inside the tick --
// is enforced by the drain's placement in PlayerbotWorldScript::OnUpdate and by
// the name HandleRemoteCommandOnWorldThread, not by an assertion.
//
// What each test is guarding, in order:
//
//   * every request gets exactly ONE reply, and the reply is the one belonging
//     to that request. A queue that mismatched request and promise would still
//     answer every caller, and would answer them with someone else's bot state.
//   * Drain(maxPerCall) runs at most maxPerCall handlers and leaves the rest
//     queued. The handler runs inside the world tick, so an off-by-one here is
//     a remote peer's ability to stall the server for as long as it likes.
//   * a full queue answers "busy" synchronously rather than growing. Unbounded
//     growth driven by a remote peer is the failure mode a naive fix ships.
//   * a future abandoned before set_value() does not crash the drainer. That is
//     the real lifetime invariant: the queue owns the promise, so a waiter that
//     times out and walks away leaves the world thread writing into a live
//     object. Get this backwards and the timeout path this fix relies on
//     becomes a second use-after-free, on the world thread this time.

#include "WorldThreadCommandQueue.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
int failures = 0;

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; \
    ++failures; \
} } while (false)

// The stand-in for HandleRemoteCommandOnWorldThread: a pure function of the
// request, so a reply can be checked against the request that produced it.
std::string Echo(std::string const& request)
{
    return "reply:" + request;
}

bool IsReady(std::future<std::string> const& pending)
{
    return pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// Every request gets exactly one reply, and it is its own.
//
// Producers retry on "busy" rather than treating it as a failure: with a queue
// cap of 256 and 1000 posts, refusals are the expected behaviour under a burst,
// not a defect. Echo() never returns "busy", so the two are distinguishable.
void TestConcurrentPostersEachGetTheirOwnReply()
{
    constexpr int kProducers = 4;
    constexpr int kPostsPerProducer = 250;
    constexpr int kTotal = kProducers * kPostsPerProducer;

    WorldThreadCommandQueue queue;
    std::atomic<int> answered{0};
    std::atomic<int> mismatches{0};

    // One drainer, standing in for the world thread. It stops once every
    // producer has been answered; the deadline is a guard against a defect in
    // the queue turning a failing test into a hung CI job.
    std::atomic<bool> stop{false};
    std::thread drainer([&]()
    {
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!stop.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        {
            queue.Drain(WorldThreadCommandQueue::MaxCommandsPerTick, Echo);
            std::this_thread::yield();
        }
        // A final pass, so nothing posted between the last Drain() and the stop
        // flag is left with an unsatisfied promise for a producer to block on.
        queue.Drain(WorldThreadCommandQueue::MaxQueuedCommands, Echo);
    });

    std::vector<std::thread> producers;
    for (int producer = 0; producer < kProducers; ++producer)
    {
        producers.emplace_back([&, producer]()
        {
            for (int post = 0; post < kPostsPerProducer; ++post)
            {
                std::string const request = std::to_string(producer) + ":" + std::to_string(post);
                std::string reply;
                do
                {
                    reply = queue.Post(request).get();
                } while (reply == WorldThreadCommandQueue::BusyReply);

                if (reply != Echo(request))
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                answered.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (std::thread& producer : producers)
        producer.join();

    stop.store(true, std::memory_order_release);
    drainer.join();

    CHECK(answered.load() == kTotal);
    CHECK(mismatches.load() == 0);
    CHECK(queue.Size() == 0);
}

// Drain(maxPerCall) runs at most maxPerCall handlers and leaves the rest.
void TestDrainRespectsPerCallCap()
{
    WorldThreadCommandQueue queue;

    std::vector<std::future<std::string>> pending;
    for (int post = 0; post < 10; ++post)
        pending.push_back(queue.Post(std::to_string(post)));
    CHECK(queue.Size() == 10);

    int handled = 0;
    auto counting = [&handled](std::string const& request)
    {
        ++handled;
        return Echo(request);
    };

    queue.Drain(4, counting);
    CHECK(handled == 4);
    CHECK(queue.Size() == 6);

    // The four that ran are the four that were posted first, in order, and only
    // those four are satisfied.
    for (std::size_t index = 0; index < pending.size(); ++index)
        CHECK(IsReady(pending[index]) == (index < 4));
    for (std::size_t index = 0; index < 4; ++index)
        CHECK(pending[index].get() == Echo(std::to_string(index)));

    // A cap larger than the queue drains what is there and no more.
    queue.Drain(WorldThreadCommandQueue::MaxCommandsPerTick, counting);
    CHECK(handled == 10);
    CHECK(queue.Size() == 0);

    // A drain of an empty queue is a no-op, not an error.
    queue.Drain(WorldThreadCommandQueue::MaxCommandsPerTick, counting);
    CHECK(handled == 10);
}

// A full queue refuses synchronously instead of growing.
void TestFullQueueRepliesBusyWithoutBlocking()
{
    WorldThreadCommandQueue queue;

    std::vector<std::future<std::string>> pending;
    for (std::size_t post = 0; post < WorldThreadCommandQueue::MaxQueuedCommands; ++post)
        pending.push_back(queue.Post(std::to_string(post)));
    CHECK(queue.Size() == WorldThreadCommandQueue::MaxQueuedCommands);

    // Nothing has drained, so none of those are answerable yet...
    CHECK(!IsReady(pending.front()));

    // ...but the refusal is answered on the spot, by the posting thread, with
    // no drain anywhere in sight. That is what keeps a connection thread from
    // blocking for the waiter timeout just to be told the server is busy.
    std::future<std::string> refused = queue.Post("one too many");
    CHECK(IsReady(refused));
    CHECK(refused.get() == std::string(WorldThreadCommandQueue::BusyReply));
    CHECK(queue.Size() == WorldThreadCommandQueue::MaxQueuedCommands);

    // The refusal did not consume a slot or displace an accepted request.
    int handled = 0;
    queue.Drain(WorldThreadCommandQueue::MaxQueuedCommands,
        [&handled](std::string const& request) { ++handled; return Echo(request); });
    CHECK(handled == static_cast<int>(WorldThreadCommandQueue::MaxQueuedCommands));
    CHECK(pending.front().get() == Echo("0"));
}

// The lifetime invariant: the queue owns the promise, so a waiter that gives up
// and destroys its future leaves the drainer writing into a live object.
void TestAbandonedFutureDoesNotCrashTheDrainer()
{
    WorldThreadCommandQueue queue;

    // Three requests: the first and last are abandoned the way a timed-out
    // connection thread abandons its future, the middle one is still waited on.
    // The abandoned entries surround the kept one so a drainer that stopped at
    // the first dead waiter would be caught.
    {
        std::future<std::string> abandoned = queue.Post("abandoned-first");
        (void)abandoned;
    }
    std::future<std::string> kept = queue.Post("kept");
    {
        std::future<std::string> abandoned = queue.Post("abandoned-last");
        (void)abandoned;
    }
    CHECK(queue.Size() == 3);

    int handled = 0;
    queue.Drain(WorldThreadCommandQueue::MaxCommandsPerTick,
        [&handled](std::string const& request) { ++handled; return Echo(request); });

    // Every entry ran, including the two nobody is listening for, and the
    // survivor's reply is intact.
    CHECK(handled == 3);
    CHECK(queue.Size() == 0);
    CHECK(kept.get() == Echo("kept"));

    // The same race, run for real: a waiter that times out and destroys its
    // future while the drainer is on its way to that very promise.
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        std::future<std::string> racing = queue.Post("racing");
        std::thread drainer([&queue]()
        {
            queue.Drain(WorldThreadCommandQueue::MaxCommandsPerTick, Echo);
        });
        {
            std::future<std::string> doomed = std::move(racing);
        }
        drainer.join();
    }
    CHECK(queue.Size() == 0);
}

// A handler that throws must not unwind the world tick.
void TestThrowingHandlerIsContained()
{
    WorldThreadCommandQueue queue;
    std::future<std::string> pending = queue.Post("boom");

    queue.Drain(WorldThreadCommandQueue::MaxCommandsPerTick,
        [](std::string const&) -> std::string { throw std::runtime_error("handler"); });

    CHECK(queue.Size() == 0);
    CHECK(IsReady(pending));
    CHECK(pending.get() == std::string(WorldThreadCommandQueue::HandlerFailedReply));
}
}

int main()
{
    TestConcurrentPostersEachGetTheirOwnReply();
    TestDrainRespectsPerCallCap();
    TestFullQueueRepliesBusyWithoutBlocking();
    TestAbandonedFutureDoesNotCrashTheDrainer();
    TestThrowingHandlerIsContained();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "WORLD_THREAD_COMMAND_QUEUE_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
