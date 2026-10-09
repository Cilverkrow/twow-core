// twow-repo#563 (X3b): age check of published snapshots and a publish/read race on the shared_ptr pattern
// PlayerbotAI uses (swap under a mutex, readers copy the pointer under it).
#include <cstdint>
#include <cstdlib>
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

    std::cout << "x3b_snapshot_policy_tests passed\n";
    return 0;
}
