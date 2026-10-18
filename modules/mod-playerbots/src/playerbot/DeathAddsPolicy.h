#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai::death_adds
{
// twow-repo#602 ROT-01 F6: the adds column of deaths.csv. The death itself clears the bot's attacker set and every mob's victim
// (Unit::SetDeathState -> CombatStop / DeleteThreatList) before OnDeath runs on the next AI tick, so "units whose victim is the
// bot" was always empty there (adds=0 in every row). The bot keeps a small snapshot of the creatures attacking it while it fights
// and OnDeath reads it. Pure rules, no game types, no locks, no config reads.
constexpr std::uint32_t RefreshMs = 1000;   // at most one snapshot a second, sooner when the attacker count changes
constexpr std::uint32_t MaxAgeMs = 5000;    // older than this at OnDeath = not the fight the bot died in
constexpr std::size_t MaxUnits = 16;        // bounded; a bot with more attackers than this is dead anyway

struct Snapshot
{
    std::array<std::uint64_t, MaxUnits> guids{};   // raw creature guids
    std::uint8_t count = 0;
    std::uint32_t attackers = 0;   // size of the attacker set when taken (players included), for the refresh rule
    std::uint32_t takenMs = 0;
    bool taken = false;

    void Clear()
    {
        count = 0;
        attackers = 0;
        taken = false;
    }

    void Begin(std::uint32_t nowMs, std::uint32_t attackerCount)
    {
        count = 0;
        attackers = attackerCount;
        takenMs = nowMs;
        taken = true;
    }

    bool Add(std::uint64_t guid)
    {
        if (count >= MaxUnits)
            return false;
        guids[count++] = guid;
        return true;
    }
};

// A new snapshot while the bot fights: none yet, the attacker count changed (an add joined or left), or the last one is a second old.
// Millisecond timer math is unsigned, so a getMSTime wrap does not stall the refresh.
inline bool Due(Snapshot const& s, std::uint32_t nowMs, std::uint32_t attackerCount)
{
    return !s.taken || attackerCount != s.attackers || std::uint32_t(nowMs - s.takenMs) >= RefreshMs;
}

inline bool Fresh(Snapshot const& s, std::uint32_t nowMs)
{
    return s.taken && std::uint32_t(nowMs - s.takenMs) <= MaxAgeMs;
}

// The adds at a death: the units that still have the bot as victim (the old rule, kept in case the core ever keeps it), then the fresh
// snapshot; each guid once, the bot's current target (the column before) never. That target need not be the killer: a mob B that kills
// the bot while it targets A is counted as an add. Order is kept: live first, then snapshot order.
inline std::vector<std::uint64_t> MergeAdds(std::vector<std::uint64_t> const& live, Snapshot const& s, bool fresh, std::uint64_t currentTarget)
{
    std::vector<std::uint64_t> adds;
    auto const push = [&adds, currentTarget](std::uint64_t guid)
    {
        if (!guid || guid == currentTarget)
            return;
        for (std::uint64_t const known : adds)
            if (known == guid)
                return;
        adds.push_back(guid);
    };
    for (std::uint64_t const guid : live)
        push(guid);
    if (fresh)
        for (std::uint8_t i = 0; i < s.count; ++i)
            push(s.guids[i]);
    return adds;
}
}
