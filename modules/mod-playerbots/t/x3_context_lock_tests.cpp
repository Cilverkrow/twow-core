// twow-repo#563 (X3a): NamedObjectContext with the per-context lock - several threads create and look up
// values while the owner updates and evicts. Without the lock this is a std::map race (crash class X1/X4).
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

typedef std::int32_t int32;
typedef std::uint32_t uint32;
typedef std::uint8_t uint8;
typedef std::int8_t int8;
typedef std::uint16_t uint16;
typedef std::int16_t int16;
typedef std::uint64_t uint64;
typedef std::int64_t int64;
class PlayerbotAI;

#include "strategy/NamedObjectContext.h"

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

std::atomic<int> alive{0};

struct Dummy
{
    Dummy() { ++alive; }
    virtual ~Dummy() { --alive; }
    void Update() { ++updates; }
    void Reset() { ++resets; }
    std::atomic<int> updates{0};
    std::atomic<int> resets{0};
};

class Context : public ai::NamedObjectContext<Dummy>
{
public:
    Context() { creators["v"] = [](PlayerbotAI*) { return new Dummy(); }; }
};
}

int main()
{
    // Off: the old single-threaded behaviour.
    {
        ai::context_lock::Enabled() = false;
        Context context;
        Dummy* a = context.Create("v::1", nullptr);
        Require(a && context.Create("v::1", nullptr) == a, "off: same object for the same name");
        Require(context.Create("unknown", nullptr) == nullptr, "off: unknown name gives null");
        Require(context.IsCreated("unknown"), "off: unknown names are remembered as before");
        context.Erase("v::1");
        Require(!context.IsCreated("v::1"), "off: erased");
    }
    Require(alive.load() == 0, "off: nothing leaked");

    // On: the same results single-threaded.
    {
        ai::context_lock::Enabled() = true;
        Context context;
        Dummy* a = context.Create("v::1", nullptr);
        Require(a && context.Create("v::1", nullptr) == a, "on: same object for the same name");
        Require(context.Create("unknown", nullptr) == nullptr && context.IsCreated("unknown"), "on: unknown remembered");
        context.Update();
        context.Reset();
        Require(a->updates.load() == 1 && a->resets.load() == 1, "on: Update/Reset reach the object");
        Require(context.EraseIf([a](Dummy* d) { return d == a; }) == 1 && !context.IsCreated("v::1"), "on: EraseIf");
    }
    Require(alive.load() == 0, "on: nothing leaked");

    // On: other threads create and look up while the owner updates and evicts.
    {
        ai::context_lock::Enabled() = true;
        Context context;
        std::atomic<bool> done{false};
        std::vector<std::thread> others;
        for (int t = 0; t < 8; ++t)
            others.emplace_back([&context, t]()
            {
                for (int i = 0; i < 20000; ++i)
                {
                    std::string const name = "v::" + std::to_string((i * 7 + t) % 300);
                    Dummy* first = context.Create(name, nullptr);
                    Require(first != nullptr, "concurrent create gives an object");
                    context.IsCreated(name);
                }
            });
        std::thread owner([&context, &done]()
        {
            int round = 0;
            while (!done.load())
            {
                context.Update();
                context.EraseIf([round](Dummy* d) { return (reinterpret_cast<std::uintptr_t>(d) >> 4) % 3 == unsigned(round % 3); });
                context.CreatedCount();
                ++round;
            }
        });
        for (std::thread& t : others)
            t.join();
        done = true;
        owner.join();

        Require(context.CreatedCount() <= 300, "never more objects than names");
        // Racing creators of one name: exactly one object ends up in the map, the losers are deleted.
        std::vector<Dummy*> seen(8, nullptr);
        std::vector<std::thread> racers;
        for (int t = 0; t < 8; ++t)
            racers.emplace_back([&context, &seen, t]() { seen[t] = context.Create("v::race", nullptr); });
        for (std::thread& t : racers)
            t.join();
        for (Dummy* d : seen)
            Require(d == seen[0], "all racers get the one object in the map");
    }
    Require(alive.load() == 0, "concurrent: every object deleted exactly once");

    ai::context_lock::Enabled() = false;
    std::cout << "x3_context_lock_tests passed\n";
    return 0;
}
