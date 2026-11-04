// twow-repo#563 X5 (CLI-ROT 02.11.2026, OB-00): AiPlayerbot.ThreadSafeTeleportAck - a master's holder (it runs on the master's
// map thread) hands the near teleport of a bot in world to the bot's own update; the random bot holder stays as before.
// twow-repo#676 (CLI-ROT 05.11.2026, BOT-HOLDER-FARACK-01, no switch): a master's holder hands every far teleport to the
// world-thread drain (guid queue), whatever the X5 switch; the holder itself never acks a far teleport.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

#include "TeleportAckThreadPolicy.h"

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
    using namespace ai::teleport_ack;

    // The random bot holder (world-thread phase, not parallel with the maps) acks every pending teleport itself, either switch.
    for (int mask = 0; mask < 16; ++mask)
    {
        bool const on = mask & 1, inWorld = mask & 2, nearTp = mask & 4, farTp = mask & 8;
        Require(Choose(on, false, inWorld, nearTp, farTp) == Acker::Holder, "random bot holder: always the holder");
    }

    // #676: a master's holder never acks a far teleport itself - in or out of world, X5 on or off (the 04.11. gap: Map::Add,
    // then Reset/StopMoving on the master's thread while the destination worker updates the bot).
    for (int mask = 0; mask < 4; ++mask)
    {
        bool const on = mask & 1, inWorld = mask & 2;
        Require(Choose(on, true, inWorld, false, true) == Acker::WorldThread, "master holder: far -> the world thread");
    }

    // X5 off, master's holder: a near teleport stays with the holder (as before; X5 is the switch for that).
    Require(Choose(false, true, true, true, false) == Acker::Holder, "X5 off: near in world stays with the holder");
    Require(Choose(false, true, false, true, false) == Acker::Holder, "X5 off: near out of world stays with the holder");
    Require(Choose(false, true, true, false, false) == Acker::Holder, "X5 off: no flag -> the holder");

    // X5 on, master's holder: a near teleport of a bot in world goes to the bot's own thread (the 02.11. crash path).
    Require(Choose(true, true, true, true, false) == Acker::BotThread, "X5 on: near in world -> the bot's thread");
    // Out of world for any other reason (login, limbo): the holder, as before.
    Require(Choose(true, true, false, true, false) == Acker::Holder, "X5 on: near but out of world -> the holder");
    // No teleport flag at all (the caller only asks for a pending teleport): the holder.
    Require(Choose(true, true, true, false, false) == Acker::Holder, "X5 on: no near / far flag -> the holder");

    // The bot's own update acks only a request, only in world, only while the near teleport is pending.
    Require(BotThreadAcks(true, true, true), "bot thread: requested, in world, near pending -> ack");
    Require(!BotThreadAcks(false, true, true), "bot thread: not requested -> no ack");
    Require(!BotThreadAcks(true, false, true), "bot thread: out of world -> no ack");
    Require(!BotThreadAcks(true, true, false), "bot thread: teleport already done -> no ack");

    // The world-thread drain acks only a bot found with its AI, out of world, far teleport still pending.
    Require(WorldThreadAcks(true, false, true), "drain: found, out of world, far pending -> ack");
    Require(!WorldThreadAcks(false, false, true), "drain: logged out / no AI -> no ack");
    Require(!WorldThreadAcks(true, true, true), "drain: still in world -> no ack (queued again by the next holder pass)");
    Require(!WorldThreadAcks(true, false, false), "drain: far teleport already acked or cancelled -> no ack");

    // Exactly one side acks a far teleport under a master's holder: not the holder, the drain does.
    Require(Choose(true, true, false, false, true) != Acker::Holder && WorldThreadAcks(true, false, true),
        "far out of world: the holder does not ack, the world-thread drain does");

    // The queue: once per guid until taken, push order kept, a depth limit, empty after Take.
    {
        FarAckQueue q;
        Require(q.Empty(), "queue starts empty");
        Require(q.Push(4694) == Queued::Added, "first push added");
        Require(!q.Empty(), "not empty after a push");
        Require(q.Push(4694) == Queued::Duplicate, "the same guid again is a duplicate");
        Require(q.Push(17) == Queued::Added, "another guid added");
        Require(q.Push(99, 2) == Queued::Full, "at the depth limit a new guid is refused");
        Require(q.Push(17, 2) == Queued::Duplicate, "a queued guid is a duplicate, also at the limit");
        std::vector<std::uint32_t> const taken = q.Take();
        Require(taken.size() == 2 && taken[0] == 4694 && taken[1] == 17, "Take returns the guids in push order");
        Require(q.Empty() && q.Take().empty(), "empty after Take");
        Require(q.Push(4694) == Queued::Added, "a taken guid can be queued again");
    }

    // The queue under concurrent producers (several masters' map threads): every guid exactly once, no loss.
    {
        FarAckQueue q;
        constexpr int kThreads = 8;
        constexpr std::uint32_t kGuids = 500;
        std::vector<std::thread> producers;
        for (int t = 0; t < kThreads; ++t)
            producers.emplace_back([&q]() {
                for (std::uint32_t g = 1; g <= kGuids; ++g)
                    q.Push(g);
            });
        for (std::thread& p : producers)
            p.join();
        std::vector<std::uint32_t> const taken = q.Take();
        std::set<std::uint32_t> const unique(taken.begin(), taken.end());
        Require(taken.size() == kGuids && unique.size() == kGuids, "concurrent pushes: every guid exactly once");
    }

    // The process queue is one object.
    Require(&FarAcks() == &FarAcks(), "FarAcks is a single queue");

    // Counters start at zero and count up.
    Counters& c = Global();
    Require(c.requested.load() == 0 && c.botAcks.load() == 0 && c.waits.load() == 0, "X5 counters start at 0");
    Require(c.farQueued.load() == 0 && c.farWorldAcks.load() == 0 && c.farSkips.load() == 0 && c.farFull.load() == 0,
        "#676 counters start at 0");
    c.requested.fetch_add(2);
    c.botAcks.fetch_add(1);
    c.farQueued.fetch_add(3);
    Require(c.requested.exchange(0) == 2 && c.botAcks.exchange(0) == 1 && c.requested.load() == 0, "counters are increments");
    Require(c.farQueued.exchange(0) == 3 && c.farQueued.load() == 0, "far counters are increments");

    std::cout << "teleport_ack_thread_policy_tests: ok\n";
    return 0;
}
