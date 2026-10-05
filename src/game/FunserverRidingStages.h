#ifndef TW_FUNSERVER_RIDING_STAGES_H
#define TW_FUNSERVER_RIDING_STAGES_H

#include <algorithm>
#include <cstdint>

// twow-repo#295, owner decisions 2026-10-02: riding in four stages and stronger player
// snares. Pure rules without server types, shared by the core and the playerbots.
//
// Riding: skill 762 values 75/150/225/300, trained at level 10/20/40/60. The server picks
// the mounted speed of a player mount aura from the learned rank and the mount family:
//   family 1 (slow mounts)  +60 / +100 / +100 / +100 %
//   family 2 (swift mounts) +60 / +100 / +140 / +180 %
// Rank 0 keeps the inherited Turtle rule (level / 2). SPELL_CUSTOM_MOUNT_SPEED_100 mounts are
// family 2 and never drop below +100 %, so characters that keep 75/150 lose no speed.
// With the switch off the inherited values stay (75 -> 60, 150 and above -> 100), without
// the old fallback that unmounted every other skill value.
//
// Snares: a slow (negative SPELL_AURA_MOD_DECREASE_SPEED amount) cast by a player-controlled
// unit is scaled by (100 + slowPct) / 100 and capped at maxSlowPct; a root
// (SPELL_AURA_MOD_ROOT) cast by a player-controlled unit lasts (100 + rootPct) / 100 as long.
// Values that are already stronger than the cap are kept. 0 % keeps every value.
namespace FunserverRiding
{
    uint32_t constexpr SKILL_ID = 762;
    uint32_t constexpr RANKS = 4;
    uint32_t constexpr RANK_SKILL[RANKS] = { 75, 150, 225, 300 };
    uint32_t constexpr RANK_LEVEL[RANKS] = { 10, 20, 40, 60 };
    uint32_t constexpr RANK_TRAINING_COST_COPPER[RANKS] = { 5000, 50000, 500000, 5000000 };
    int32_t constexpr FAMILY1_SPEED[RANKS] = { 60, 100, 100, 100 };
    int32_t constexpr FAMILY2_SPEED[RANKS] = { 60, 100, 140, 180 };

    // A mount spell whose own aura-32 value is at least this is a swift (family 2) mount.
    int32_t constexpr FAMILY2_MIN_BASE_SPEED = 100;
    int32_t constexpr SPEED_100 = 100;

    // Mount purchase prices of the racial vendors (mount 1 / mount 2).
    uint32_t constexpr MOUNT1_PRICE_COPPER = 10000;
    uint32_t constexpr MOUNT2_PRICE_COPPER = 1000000;
    // Family 2 mounts need rank 3 (riding 225); family 1 mounts need rank 1 (riding 75).
    uint32_t constexpr FAMILY1_REQUIRED_SKILL = 75;
    uint32_t constexpr FAMILY2_REQUIRED_SKILL = 225;

    enum class MountFamily : uint8_t
    {
        Slow  = 1,
        Swift = 2,
    };

    // 0 (no riding) .. 4 (riding 300) from the pure riding skill value.
    inline uint32_t RankFromSkill(uint32_t skillValue)
    {
        uint32_t rank = 0;
        for (uint32_t r = 0; r < RANKS; ++r)
            if (skillValue >= RANK_SKILL[r])
                rank = r + 1;
        return rank;
    }

    inline MountFamily FamilyOf(int32_t baseSpeed, bool speed100Flag)
    {
        return (speed100Flag || baseSpeed >= FAMILY2_MIN_BASE_SPEED) ? MountFamily::Swift : MountFamily::Slow;
    }

    // Mounted speed bonus in percent for a player's mount aura (aura 32 on a spell whose
    // effect 0 is SPELL_AURA_MOUNTED). baseSpeed is the spell's own aura-32 value.
    inline int32_t MountedSpeedPct(bool stagesEnabled, uint32_t skillValue, uint32_t level,
        int32_t baseSpeed, bool speed100Flag)
    {
        uint32_t const rank = RankFromSkill(skillValue);
        if (rank == 0)
            return speed100Flag ? SPEED_100 : static_cast<int32_t>(level / 2);
        if (!stagesEnabled)
            return speed100Flag ? SPEED_100 : (rank == 1 ? FAMILY1_SPEED[0] : FAMILY1_SPEED[1]);

        int32_t speed = FamilyOf(baseSpeed, speed100Flag) == MountFamily::Swift
            ? FAMILY2_SPEED[rank - 1]
            : FAMILY1_SPEED[rank - 1];
        if (speed100Flag)
            speed = std::max(speed, SPEED_100);
        return speed;
    }

    // Highest rank a character of this level may train (0 below level 10).
    inline uint32_t MaxRankForLevel(uint32_t level)
    {
        uint32_t rank = 0;
        for (uint32_t r = 0; r < RANKS; ++r)
            if (level >= RANK_LEVEL[r])
                rank = r + 1;
        return rank;
    }
}

namespace FunserverSnare
{
    uint32_t constexpr DEFAULT_SLOW_PCT = 0;
    uint32_t constexpr DEFAULT_MAX_SLOW_PCT = 90;
    uint32_t constexpr DEFAULT_ROOT_DURATION_PCT = 0;

    // amount is the (negative) SPELL_AURA_MOD_DECREASE_SPEED value, e.g. -50 for a 50 % slow.
    inline int32_t ScaleSlow(int32_t amount, uint32_t slowPct, uint32_t maxSlowPct)
    {
        if (amount >= 0 || slowPct == 0)
            return amount;
        int64_t scaled = static_cast<int64_t>(amount) * (100 + static_cast<int64_t>(slowPct)) / 100;
        int64_t const cap = -static_cast<int64_t>(std::min<uint32_t>(maxSlowPct, 100));
        if (scaled < cap)
            scaled = cap;
        if (scaled > amount)    // never weaker than the original slow
            scaled = amount;
        return static_cast<int32_t>(scaled);
    }

    // durationMs <= 0 (instant or permanent) stays as it is.
    inline int32_t ScaleRootDuration(int32_t durationMs, uint32_t rootPct)
    {
        if (durationMs <= 0 || rootPct == 0)
            return durationMs;
        int64_t const scaled = static_cast<int64_t>(durationMs) * (100 + static_cast<int64_t>(rootPct)) / 100;
        return static_cast<int32_t>(std::min<int64_t>(scaled, INT32_MAX));
    }
}

#endif
