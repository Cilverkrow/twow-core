#include "BotPacketInbox.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

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
}

int main()
{
    using ai::BoundedInbox;

    // Order and drain.
    {
        BoundedInbox<int> inbox(4);
        inbox.Push(1);
        inbox.Push(2);
        inbox.Push(3);
        std::vector<int> out;
        inbox.Drain(out);
        Require(out == std::vector<int>({1, 2, 3}), "arrival order");
        Require(inbox.Size() == 0, "empty after drain");
        inbox.Drain(out);
        Require(out.size() == 3, "draining an empty inbox adds nothing");
    }

    // Capacity: the oldest are dropped and counted.
    {
        BoundedInbox<int> inbox(3);
        for (int i = 1; i <= 3; ++i)
            Require(inbox.Push(i) == 0, "nothing dropped below the bound");
        Require(inbox.Push(4) == 1, "Push hands back the dropped (oldest) item");
        Require(inbox.Push(5) == 2, "and the next oldest");
        Require(inbox.Size() == 3, "bounded");
        Require(inbox.Dropped() == 2, "two dropped");
        std::vector<int> out;
        inbox.Drain(out);
        Require(out == std::vector<int>({3, 4, 5}), "the newest are kept");
    }

    // Capacity 0 is treated as 1.
    {
        BoundedInbox<int> inbox(0);
        inbox.Push(7);
        inbox.Push(8);
        std::vector<int> out;
        inbox.Drain(out);
        Require(out == std::vector<int>({8}), "capacity at least 1");
    }

    // Move-only items (the bot queues unique_ptr<WorldPacket>).
    {
        BoundedInbox<std::unique_ptr<int>> inbox(2);
        inbox.Push(std::make_unique<int>(5));
        std::vector<std::unique_ptr<int>> out;
        inbox.Drain(out);
        Require(out.size() == 1 && *out[0] == 5, "move-only items");
    }

    // Many senders, one bot draining: nothing lost besides the counted drops.
    {
        constexpr int Senders = 8;
        constexpr int PerSender = 20000;
        BoundedInbox<int> inbox(ai::BotPacketInboxCapacity);
        ai::InboxDroppedTotal() = 0;
        ai::InboxLargestDrain() = 0;
        std::atomic<bool> done{false};
        std::atomic<long long> drained{0};

        std::thread bot([&]()
        {
            std::vector<int> out;
            while (!done.load())
            {
                out.clear();
                inbox.Drain(out);
                drained += (long long)out.size();
            }
            out.clear();
            inbox.Drain(out);
            drained += (long long)out.size();
        });

        std::vector<std::thread> senders;
        for (int s = 0; s < Senders; ++s)
            senders.emplace_back([&]()
            {
                for (int i = 0; i < PerSender; ++i)
                    inbox.Push(i);
            });
        for (std::thread& t : senders)
            t.join();
        done = true;
        bot.join();

        Require(drained.load() + (long long)inbox.Dropped() == (long long)Senders * PerSender,
            "every pushed item is drained or counted as dropped");
        Require(ai::InboxDroppedTotal().load() == inbox.Dropped(), "server-wide drop counter");
        Require(ai::InboxLargestDrain().load() <= ai::BotPacketInboxCapacity, "a drain never exceeds the bound");
        Require(ai::InboxLargestDrain().load() > 0, "largest drain recorded");
    }

    std::cout << "bot_packet_inbox_tests passed\n";
    return 0;
}
