#pragma once

// twow-repo#588 (BUFF-01, owner 14.10.2026: "viele bots buffen sich regelmäßig gegenseitig oder nur wenn ich in der nähe
// bin"): every solo bot buffed every friendly player within sight who lacked its class buff - other bots, the owner,
// strangers ("friendly unit without aura" -> nearest friendly players). With AiPlayerbot.Buff.RealPlayerStyle (default
// 0) the order is like a real player's: self and group, then the bot's guild, and a stranger only once per
// StrangerCooldownSeconds per bot. Counters for the [BuffTargets] minute line.

#include <atomic>
#include <cstdint>

namespace ai::buff_target
{
    enum class Category { Group, Guild, StrangerBot, StrangerPlayer, StrangerOnCooldown, Count };

    inline bool StrangerAllowed(std::uint32_t now, std::uint32_t nextAllowed)
    {
        return nextAllowed == 0 || now >= nextAllowed;
    }

    inline std::uint32_t NextStrangerAllowed(std::uint32_t now, std::uint32_t cooldownSeconds)
    {
        return now + cooldownSeconds;
    }

    inline bool SameGuild(std::uint32_t botGuild, std::uint32_t otherGuild)
    {
        return botGuild != 0 && botGuild == otherGuild;
    }

    inline std::atomic<std::uint64_t>& Counter(Category category)
    {
        static std::atomic<std::uint64_t> counters[int(Category::Count)] = {};
        return counters[int(category)];
    }

    inline void Count(Category category)
    {
        Counter(category).fetch_add(1, std::memory_order_relaxed);
    }
}
