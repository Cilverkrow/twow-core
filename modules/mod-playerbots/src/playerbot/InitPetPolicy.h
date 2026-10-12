#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace ai::init_pet
{
// twow-repo#541 (roster spikes: "initialize pet" ~70 ms, OB-00 go 10.10.2026), behind
// AiPlayerbot.InitPet.Cache (default 0):
// 1. PlayerbotFactory::InitPet scanned every creature template per call for tameable ones within the
//    bot's level. The tameable entries are now collected once, sorted by MinLevel; the candidates for a
//    level are a prefix of that list - the same set as the scan, so the random pick is unchanged.
// 2. InitializePetAction::isUseful queried character_pet on every check (trigger "often"); the answer
//    is kept per bot for StoredPetCacheSeconds.
// 3. After an InitPet that left the hunter without a pet, isUseful says no for NoPetCooldownSeconds.
constexpr std::uint32_t StoredPetCacheSeconds = 60;
constexpr std::uint32_t NoPetCooldownSeconds = 60;

// (MinLevel, entry), sorted by MinLevel then entry.
using TameList = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

inline void SortTameList(TameList& list)
{
    std::sort(list.begin(), list.end());
}

// How many entries of the sorted list a bot of this level may tame (MinLevel <= level).
inline std::size_t CandidatesFor(TameList const& sorted, std::uint32_t level)
{
    auto const end = std::upper_bound(sorted.begin(), sorted.end(), std::make_pair(level, UINT32_MAX));
    return std::size_t(end - sorted.begin());
}

// A cached answer is still valid.
inline bool CacheValid(std::uint32_t checkedAt, std::uint32_t now, std::uint32_t seconds)
{
    return checkedAt && now >= checkedAt && now - checkedAt < seconds;
}

// Still within the cooldown after a failed InitPet.
inline bool InCooldown(std::uint32_t until, std::uint32_t now)
{
    return until && now < until;
}
}
