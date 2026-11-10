#pragma once

#include <cstdint>

// twow-repo#698: the roster list is built in the issuing player's command thread, not on the bots' map threads. It used to read each online
// bot's strategy map (PlayerbotAI::IsTankFor/IsHealFor -> Engine::ContainsStrategy) and its Group (IsRaidGroup) from there while the bot's
// own thread changes both: the same crash class as #697. Each bot now publishes a small snapshot - role and group state - from its OWN thread;
// readers on other threads only load one atomic word. Pure packing logic, no game types.
namespace ai::roster_snapshot
{
// codes, independent of the roster list enums (the call site maps them)
enum : uint32_t { RoleDps = 0, RoleTank = 1, RoleHeal = 2 };
enum : uint32_t { GroupFree = 0, GroupParty = 1, GroupRaid = 2 };

constexpr uint32_t ValidBit = 0x80u;           // 0 as a whole word = never written (bot just logged in)
constexpr uint32_t RoleMask = 0x03u;           // bits 0-1
constexpr uint32_t GroupShift = 2u;            // bits 2-3
constexpr uint32_t GroupMask = 0x03u;

// minimum time between two refreshes on the bot's thread (milliseconds); a role changes with strategy switches, not per tick
constexpr uint32_t RefreshIntervalMs = 2000;

inline uint32_t Pack(uint32_t role, uint32_t group)
{
    return ValidBit | (role & RoleMask) | ((group & GroupMask) << GroupShift);
}

struct Unpacked
{
    bool valid = false;
    uint32_t role = RoleDps;
    uint32_t group = GroupFree;
};

inline Unpacked Unpack(uint32_t word)
{
    Unpacked u;
    if (!(word & ValidBit))
        return u;                    // never written: the same answer as for an offline member (DPS, no group)
    u.valid = true;
    u.role = word & RoleMask;
    u.group = (word >> GroupShift) & GroupMask;
    return u;
}

// true when the snapshot is due: first write, or RefreshIntervalMs passed (wrap-safe)
inline bool RefreshDue(uint32_t lastMs, uint32_t nowMs, bool everWritten)
{
    return !everWritten || uint32_t(nowMs - lastMs) >= RefreshIntervalMs;
}
}
