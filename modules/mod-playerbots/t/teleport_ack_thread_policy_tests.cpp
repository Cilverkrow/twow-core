// twow-repo#563 X5 (CLI-ROT 02.11.2026, OB-00): AiPlayerbot.ThreadSafeTeleportAck - a master's holder (it runs on the master's
// map thread) hands the near teleport of a bot in world to the bot's own update; far teleports and the random bot holder stay as
// before; off changes nothing.
#include <cstdlib>
#include <iostream>

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

    // Off: the holder acks every pending teleport, whatever the bot's state (as before).
    for (int mask = 0; mask < 16; ++mask)
    {
        bool const master = mask & 1, inWorld = mask & 2, nearTp = mask & 4, farTp = mask & 8;
        Require(Choose(false, master, inWorld, nearTp, farTp) == Acker::Holder, "off: always the holder");
    }

    // The random bot holder (world-thread phase, not parallel with the maps) keeps acking.
    Require(Choose(true, false, true, true, false) == Acker::Holder, "random bot holder: near in world stays with the holder");
    Require(Choose(true, false, true, false, true) == Acker::Holder, "random bot holder: far stays with the holder");

    // A master's holder: a near teleport of a bot in world goes to the bot's own thread (the 02.11. crash path).
    Require(Choose(true, true, true, true, false) == Acker::BotThread, "master holder: near in world -> the bot's thread");
    // A far teleport has left the old map: no map thread owns the bot, the holder acks it.
    Require(Choose(true, true, false, false, true) == Acker::Holder, "master holder: far out of world -> the holder");
    // Out of world for any other reason (login, limbo): the holder, as before.
    Require(Choose(true, true, false, true, false) == Acker::Holder, "master holder: near but out of world -> the holder");
    // A far teleport still seen in world: nobody acks it this pass.
    Require(Choose(true, true, true, false, true) == Acker::Wait, "master holder: far but still in world -> wait");
    // No teleport flag at all (the caller only asks for a pending teleport): the holder.
    Require(Choose(true, true, true, false, false) == Acker::Holder, "master holder: no near / far flag -> the holder");

    // The bot's own update acks only a request, only in world, only while the near teleport is pending.
    Require(BotThreadAcks(true, true, true), "bot thread: requested, in world, near pending -> ack");
    Require(!BotThreadAcks(false, true, true), "bot thread: not requested -> no ack");
    Require(!BotThreadAcks(true, false, true), "bot thread: out of world -> no ack");
    Require(!BotThreadAcks(true, true, false), "bot thread: teleport already done -> no ack");

    // Exactly one side acks a near teleport of a bot in world under a master's holder.
    Require(Choose(true, true, true, true, false) != Acker::Holder && BotThreadAcks(true, true, true),
        "near in world: the holder does not ack, the bot thread does");

    // Counters start at zero and count up.
    Counters& c = Global();
    Require(c.requested.load() == 0 && c.botAcks.load() == 0 && c.waits.load() == 0, "counters start at 0");
    c.requested.fetch_add(2);
    c.botAcks.fetch_add(1);
    Require(c.requested.exchange(0) == 2 && c.botAcks.exchange(0) == 1 && c.requested.load() == 0, "counters are increments");

    std::cout << "teleport_ack_thread_policy_tests: ok\n";
    return 0;
}
