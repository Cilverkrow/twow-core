// twow-repo#563 (X3b): age check of published snapshots and a publish/read race on the shared_ptr pattern
// PlayerbotAI uses (swap under a mutex, readers copy the pointer under it).
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "X3bSnapshotPolicy.h"

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct Guid { std::uint64_t raw = 0; };
typedef ai::x3b::PublishedTargets<Guid> Snapshot;
}

int main()
{
    using ai::x3b::Fresh;
    using ai::x3b::MaxAgeMs;

    Require(Fresh(10000, 10000), "same ms is fresh");
    Require(Fresh(10000 + MaxAgeMs, 10000), "exactly max age is fresh");
    Require(!Fresh(10000 + MaxAgeMs + 1, 10000), "older than max age is stale");
    Require(Fresh(100, 0xFFFFFF00u), "fresh across the 32-bit wrap");
    Require(!Fresh(100 + MaxAgeMs, 0xFFFFFF00u), "stale across the 32-bit wrap");
    Require(!Fresh(5000, 9000), "a snapshot from the future (clock moved back) counts as stale");

    Snapshot empty;
    Require(empty.possibleTargets.empty() && empty.currentTarget.raw == 0 && empty.oldTarget.raw == 0, "default snapshot is empty");

    // Owner publishes while readers copy and walk the snapshot: no torn list, nothing freed under a reader.
    std::mutex mutex;
    std::shared_ptr<Snapshot const> published;
    std::thread owner([&]()
    {
        for (std::uint32_t i = 1; i <= 20000; ++i)
        {
            auto next = std::make_shared<Snapshot>();
            next->publishedMs = i;
            next->possibleTargets.assign(i % 17, Guid{ i });
            std::shared_ptr<Snapshot const> previous;
            {
                std::scoped_lock lock(mutex);
                previous.swap(published);
                published = std::move(next);
            }
        }
    });
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r)
        readers.emplace_back([&]()
        {
            for (int i = 0; i < 20000; ++i)
            {
                std::shared_ptr<Snapshot const> copy;
                {
                    std::scoped_lock lock(mutex);
                    copy = published;
                }
                if (!copy)
                    continue;
                Require(copy->possibleTargets.size() == copy->publishedMs % 17, "snapshot list matches its stamp");
                for (Guid const& guid : copy->possibleTargets)
                    Require(guid.raw == copy->publishedMs, "every entry belongs to the same snapshot");
            }
        });
    owner.join();
    for (std::thread& t : readers)
        t.join();

    // Site 2: condition board.
    {
        ai::x3b::ConditionBoard board;
        Require(!board.HasRequests(), "empty board has no requests");
        Require(!board.Read("near leader", 1000).has_value(), "first read has no answer");
        Require(board.HasRequests(), "the read registered the condition");

        int evaluations = 0;
        // The evaluation reads the same board: it must run outside the board lock (std::mutex would deadlock).
        board.Refresh(1000, [&](std::string const& condition)
        {
            ++evaluations;
            board.Read("nested", 1000);
            return condition == "near leader";
        });
        Require(evaluations == 1, "one registered condition evaluated");
        std::optional<bool> answer = board.Read("near leader", 1500);
        Require(answer.has_value() && *answer, "fresh answer after refresh");

        board.Refresh(1500, [&](std::string const&) { ++evaluations; return false; });
        Require(evaluations == 2, "only the nested condition (no answer yet) evaluated; near leader still within refresh interval");
        board.Refresh(1000 + ai::x3b::ConditionRefreshMs, [&](std::string const&) { ++evaluations; return false; });
        Require(evaluations == 3, "near leader due again after the refresh interval");
        answer = board.Read("near leader", 1000 + ai::x3b::ConditionRefreshMs);
        Require(answer.has_value() && !*answer, "answer replaced by the newer evaluation");

        Require(!board.Read("near leader", 1000 + ai::x3b::ConditionRefreshMs + ai::x3b::MaxAgeMs + 1).has_value(), "stale answer not returned");

        std::uint32_t const later = 50000;
        board.Refresh(later, [&](std::string const&) { ++evaluations; return true; });
        Require(!board.HasRequests(), "conditions nobody asked for are forgotten");

        for (std::size_t i = 0; i < ai::x3b::MaxConditionRequests + 10; ++i)
            board.Read("c" + std::to_string(i), later);
        int bounded = 0;
        board.Refresh(later, [&](std::string const&) { ++bounded; return true; });
        Require(std::size_t(bounded) == ai::x3b::MaxConditionRequests, "registrations are bounded");
    }

    // Site 2: readers on several threads while the owner refreshes.
    {
        ai::x3b::ConditionBoard board;
        std::atomic<bool> done{false};
        std::thread owner([&]()
        {
            std::uint32_t now = 1;
            while (!done.load())
                board.Refresh(now += 400, [](std::string const& condition) { return condition.size() % 2 == 0; });
        });
        std::vector<std::thread> askers;
        for (int t = 0; t < 4; ++t)
            askers.emplace_back([&board, t]()
            {
                for (int i = 0; i < 20000; ++i)
                {
                    std::string const condition = "cond" + std::string(std::size_t((i + t) % 5), 'x');
                    std::optional<bool> const value = board.Read(condition, std::uint32_t(i));
                    if (value)
                        Require(*value == (condition.size() % 2 == 0), "answer matches its condition");
                }
            });
        for (std::thread& t : askers)
            t.join();
        done = true;
        owner.join();
    }

    std::cout << "x3b_snapshot_policy_tests passed\n";
    return 0;
}
