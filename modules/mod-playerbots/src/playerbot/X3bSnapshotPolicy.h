#pragma once

// twow-repo#563 X3b, variant 2 (published snapshot, OB-00 go 10.10.2026; switch AiPlayerbot.X3b.PublishedTargets,
// default 0). A bot no longer computes values on another bot's context from its own region thread. The owner
// publishes what others need - here the targets around it - as an immutable snapshot; readers take a shared_ptr
// copy and never touch the other bot's Value objects. A snapshot older than MaxAgeMs is ignored and counted.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ai::x3b
{
    // Readers accept a snapshot this old. The owner republishes on every own attackers calculation, which
    // in combat is every few hundred ms; out of combat a stale snapshot only means "no extra targets".
    constexpr uint32_t MaxAgeMs = 3000;

    // uint32 ms clock that wraps after ~49 days; unsigned subtraction handles the wrap.
    inline bool Fresh(uint32_t nowMs, uint32_t publishedMs)
    {
        return uint32_t(nowMs - publishedMs) <= MaxAgeMs;
    }

    template <class Guid>
    struct PublishedTargets
    {
        uint32_t publishedMs = 0;
        std::vector<Guid> possibleTargets;   // the owner's own "possible targets" at AttackersValue::GetRange()
        Guid currentTarget;
        Guid oldTarget;
    };

    enum SnapshotRead { SnapshotHit, SnapshotStale, SnapshotMissing, SnapshotReads };

    inline std::atomic<uint64_t>& SnapshotCount(SnapshotRead read)
    {
        static std::atomic<uint64_t> counts[SnapshotReads] = {};
        return counts[read];
    }

    // Site 2 (GroupBool AND/OR/COUNT, switch AiPlayerbot.X3b.PublishedConditions): those values evaluated an
    // arbitrary condition ("and::<qualifier>") on every group member's context. A fixed snapshot cannot hold
    // arbitrary conditions, so each bot keeps a board: other bots read an answer and register the condition;
    // the owner evaluates the registered conditions on its own thread (Refresh) and stores the answers.
    // A member without a fresh answer is skipped by the reader, like a member without bot AI.
    constexpr uint32_t ConditionRefreshMs = 1000;   // the owner recomputes an answer at most this often
    constexpr uint32_t ConditionForgetMs = 10000;   // a condition nobody asked for this long is dropped
    constexpr size_t MaxConditionRequests = 64;      // per bot; more distinct conditions are not registered

    enum ConditionRead { ConditionHit, ConditionMiss, ConditionDropped, ConditionEvaluated, ConditionReads };

    inline std::atomic<uint64_t>& ConditionCount(ConditionRead read)
    {
        static std::atomic<uint64_t> counts[ConditionReads] = {};
        return counts[read];
    }

    class ConditionBoard
    {
    public:
        // Any thread. The answer if it is fresh; registers the condition either way.
        std::optional<bool> Read(std::string const& condition, uint32_t nowMs)
        {
            std::scoped_lock lock(mutex);
            auto request = requests.find(condition);
            if (request != requests.end())
                request->second = nowMs;
            else if (requests.size() < MaxConditionRequests)
            {
                requests.emplace(condition, nowMs);
                requestCount.store(requests.size(), std::memory_order_relaxed);
            }
            else
                ConditionCount(ConditionDropped).fetch_add(1, std::memory_order_relaxed);

            auto const answer = answers.find(condition);
            if (answer == answers.end() || !Fresh(nowMs, answer->second.second))
            {
                ConditionCount(ConditionMiss).fetch_add(1, std::memory_order_relaxed);
                return std::nullopt;
            }
            ConditionCount(ConditionHit).fetch_add(1, std::memory_order_relaxed);
            return answer->second.first;
        }

        bool HasRequests() const { return requestCount.load(std::memory_order_relaxed) != 0; }

        // Owner thread only. evaluate(condition) runs OUTSIDE the lock (it computes values that may read other
        // boards), so no thread ever holds two board locks.
        template <class Evaluate>
        void Refresh(uint32_t nowMs, Evaluate evaluate)
        {
            std::vector<std::string> due;
            {
                std::scoped_lock lock(mutex);
                for (auto request = requests.begin(); request != requests.end();)
                {
                    if (uint32_t(nowMs - request->second) > ConditionForgetMs)
                    {
                        answers.erase(request->first);
                        request = requests.erase(request);
                        continue;
                    }
                    auto const answer = answers.find(request->first);
                    if (answer == answers.end() || uint32_t(nowMs - answer->second.second) >= ConditionRefreshMs)
                        due.push_back(request->first);
                    ++request;
                }
                requestCount.store(requests.size(), std::memory_order_relaxed);
            }
            if (due.empty())
                return;

            std::vector<std::pair<std::string, bool>> computed;
            computed.reserve(due.size());
            for (std::string const& condition : due)
                computed.emplace_back(condition, bool(evaluate(condition)));
            ConditionCount(ConditionEvaluated).fetch_add(computed.size(), std::memory_order_relaxed);

            std::scoped_lock lock(mutex);
            for (auto const& [condition, value] : computed)
                answers[condition] = { value, nowMs };
        }

    private:
        std::mutex mutex;
        std::unordered_map<std::string, uint32_t> requests;                     // condition -> last asked (ms)
        std::unordered_map<std::string, std::pair<bool, uint32_t>> answers;     // condition -> value, computed at
        std::atomic<size_t> requestCount{0};
    };
}
