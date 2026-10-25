#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

class Player;

namespace ai::dungeon_run
{
// twow-repo#629 B4/D2 (CLI-GRP, DungeonClear.AutoRun): the seam between a playerbots group manager and mod-dungeon-clear.
// Playerbots does not link dungeon-clear symbols; dungeon clear (which already includes playerbots) registers its hooks here
// at config load, and posts the end of every run into a small mailbox the manager takes from. Header-only, no game types
// beyond a Player* the hooks receive.

enum class Start
{
    Off,        // AutoRun 0, no hook registered, or not a bot-only group
    Pending,    // the strategies are being installed on the members' own map threads (the gate) - ask again next pass
    Running,    // the run is enabled on the group's leader tank
    Refused,    // dungeon clear refused (no boss roster, a dead member it cannot recover, ...) - see the reason
};

using StartHook = Start (*)(Player* anyMember, std::string* why);
using StopHook = void (*)(Player* anyMember, char const* why);

struct Hooks
{
    StartHook start = nullptr;
    StopHook stop = nullptr;
};

inline Hooks& Registered()
{
    static Hooks hooks;
    return hooks;
}

// The world-thread manager calls these (maps idle, the .dc test pattern); without dungeon clear they are no-ops.
inline Start StartRun(Player* anyMember, std::string* why)
{
    StartHook const h = Registered().start;
    return h ? h(anyMember, why) : Start::Off;
}

inline void StopRun(Player* anyMember, char const* why)
{
    if (StopHook const h = Registered().stop)
        h(anyMember, why);
}

// Run ends, keyed by the leader tank's low guid; bounded (a manager that never takes them must not grow it).
constexpr std::size_t MaxEnded = 256;

struct Ended
{
    std::string reason;
    std::uint32_t at = 0;
};

class EndedMailbox
{
public:
    void Note(std::uint32_t leader, std::string reason, std::uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (ended.size() >= MaxEnded && ended.find(leader) == ended.end())
        {
            auto oldest = ended.begin();
            for (auto it = ended.begin(); it != ended.end(); ++it)
                if (it->second.at < oldest->second.at)
                    oldest = it;
            ended.erase(oldest);
        }
        ended[leader] = Ended{ std::move(reason), now };
    }

    bool Take(std::uint32_t leader, Ended& out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = ended.find(leader);
        if (it == ended.end())
            return false;
        out = std::move(it->second);
        ended.erase(it);
        return true;
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return ended.size();
    }

private:
    mutable std::mutex mutex;
    std::map<std::uint32_t, Ended> ended;
};

inline EndedMailbox& Mailbox()
{
    static EndedMailbox box;
    return box;
}
}
