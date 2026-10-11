// twow-repo#551 (AiPlayerbot.Park.Quiet): whispers only, idle emote spread by guid and at most once per interval.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>

#include "ParkPolicy.h"

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
    using namespace ai::park;

    Require(std::strstr(QuietDroppedStrategies(), "-emote") && std::strstr(QuietDroppedStrategies(), "-custom::say") &&
        std::strstr(QuietDroppedStrategies(), "-ai chat") && QuietDroppedStrategies()[0] == ',', "social strategies, appendable");

    Require(QuietDropsChatReply(true, true, false), "quiet parked bot: no reply to say/yell/channel");
    Require(!QuietDropsChatReply(true, true, true), "whispers still answered");
    Require(!QuietDropsChatReply(false, true, false), "switch off: as before");
    Require(!QuietDropsChatReply(true, false, false), "not parked: as before");

    // First emote inside one interval after parking, spread by guid; none before it; never with interval 0.
    std::uint32_t const now = 100000, interval = 600;
    std::set<std::uint32_t> firsts;
    for (std::uint32_t guid = 1; guid <= 20; ++guid)
    {
        std::uint32_t const first = FirstIdleEmote(guid, now, interval);
        Require(first > now && first <= now + interval, "first emote within one interval");
        Require(!IdleEmoteDue(first - 1, first, interval) && IdleEmoteDue(first, first, interval), "due exactly from then");
        firsts.insert(first);
    }
    Require(firsts.size() == 20, "a full inn does not emote together");
    Require(!IdleEmoteDue(now + 100000, FirstIdleEmote(1, now, 0), 0), "interval 0: never");
    Require(!IdleEmoteDue(now, 0, interval), "not scheduled (unparked): never");

    // At most one emote per interval over an hour of park updates every 5 s.
    std::uint32_t next = FirstIdleEmote(7, now, interval), emotes = 0;
    for (std::uint32_t t = now; t < now + 3600; t += 5)
        if (IdleEmoteDue(t, next, interval))
        {
            ++emotes;
            next = t + interval;
        }
    Require(emotes >= 5 && emotes <= 6, "about one emote per 10 minutes");

    std::cout << "park_quiet_policy_tests passed\n";
    return 0;
}
