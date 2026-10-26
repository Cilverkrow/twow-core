#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai::rpg_area_level
{
// twow-repo#333 P1-v (AiPlayerbot.Travel.RpgAreaLevelFromSpawn, OB-00 26.10.2026, M1r run 3): RpgTravelDestination::IsPossible
// reads the area level of GuidPosition(HIGHGUID_UNIT, entry), whose position is the default WorldPosition() - map 0 at
// (0, 0, 0), not the spawn. Whatever area lies at the map origin decided for every vendor, repairer, trainer, mailbox,
// auctioneer and named RPG target of every bot. With the switch on for the bot (0 off, 1 all, 2 cohort B) the area level
// comes from the spawn nearest to the bot. The rule itself stays: an area more than kAboveBotLevels above the bot is not
// possible; an unknown level (0 or less) never refuses.
// Pure rules, no game types.

constexpr std::int32_t kAboveBotLevels = 5;

inline bool TooHighForBot(std::int32_t areaLevel, std::uint32_t botLevel)
{
    return areaLevel > 0 && areaLevel > std::int32_t(botLevel) + kAboveBotLevels;
}

// The index of the spawn nearest to the bot: the smallest squared distance among the spawns on the bot's map; without one
// on that map the first spawn (another map: the portal distance is no measure). npos without spawns.
constexpr std::size_t npos = static_cast<std::size_t>(-1);

inline std::size_t NearestSpawn(std::vector<bool> const& onBotMap, std::vector<float> const& sqDistances)
{
    if (onBotMap.empty() || onBotMap.size() != sqDistances.size())
        return npos;
    std::size_t best = npos;
    for (std::size_t i = 0; i < onBotMap.size(); ++i)
        if (onBotMap[i] && (best == npos || sqDistances[i] < sqDistances[best]))
            best = i;
    return best == npos ? 0 : best;
}

// The purpose= field of the "[RpgAreaLevel] state=trip" line (CLI-Deep: sell / repair trips per bot-h and cohort). The bits
// of TravelDestinationPurpose (TravelValues.h): GenericRpg 1 << 6, Trainer 1 << 7, Repair 1 << 8, Vendor 1 << 9, AH 1 << 10,
// Mail 1 << 11.
inline char const* PurposeName(std::uint32_t purpose)
{
    switch (purpose)
    {
    case 1u << 6: return "GenericRpg";
    case 1u << 7: return "Trainer";
    case 1u << 8: return "Repair";
    case 1u << 9: return "Vendor";
    case 1u << 10: return "AH";
    case 1u << 11: return "Mail";
    default: return "other";
    }
}
}
