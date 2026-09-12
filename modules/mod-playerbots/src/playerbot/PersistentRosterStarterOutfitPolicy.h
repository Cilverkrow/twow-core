#pragma once

#include <cstdint>

namespace ai { namespace roster { namespace starter_outfit
{
// The policy is intentionally independent from Player and database state. The
// caller obtains every item and required quantity from PlayerCreateInfo::item.
constexpr std::uint8_t kStarterOutfitLevel = 1;

inline bool ShouldProvision(bool persistentRosterMember, std::uint8_t level)
{
    return persistentRosterMember && level == kStarterOutfitLevel;
}

inline std::uint32_t MissingAmount(std::uint32_t required, std::uint32_t owned)
{
    return owned < required ? required - owned : 0;
}

// Inventory content is the durable completion marker. It supports a failed
// partial pass without adding a new event, schema, or database write: after a
// failed StoreNewItemInBestSlots call, the next login calculates only the
// still-missing canonical quantity.
inline bool IsComplete(std::uint32_t required, std::uint32_t owned)
{
    return MissingAmount(required, owned) == 0;
}
}}} // namespace ai::roster::starter_outfit
