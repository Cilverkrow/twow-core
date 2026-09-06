#pragma once

#include <cmath>
#include <cstdint>

namespace ai
{
    // FlightPathMovementGenerator moves players at 32 yards/second. Keeping
    // route estimates in the same units as walking prevents A* from treating
    // every taxi hop as virtually free.
    constexpr float PLAYERBOT_TAXI_SPEED = 32.0f;

    inline float GetTaxiTravelTime(float pathDistance)
    {
        return pathDistance > 0.0f ? pathDistance / PLAYERBOT_TAXI_SPEED : 0.0f;
    }

    inline std::uint32_t MixTravelRouteSeed(std::uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    // Give independent parties slightly different preferences between
    // otherwise comparable graph edges. The small bound preserves sensible
    // routes, while avoiding a single deterministic corridor for every bot.
    inline float GetStableRouteCostMultiplier(std::uint32_t partySeed,
        std::uint32_t fromMap, float fromX, float fromY,
        std::uint32_t toMap, float toX, float toY)
    {
        auto quantize = [](float coordinate)
        {
            return static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::floor(coordinate)));
        };

        std::uint32_t seed = MixTravelRouteSeed(partySeed);
        seed ^= MixTravelRouteSeed(fromMap + 0x9e3779b9u);
        seed ^= MixTravelRouteSeed(quantize(fromX) + 0x85ebca6bu);
        seed ^= MixTravelRouteSeed(quantize(fromY) + 0xc2b2ae35u);
        seed ^= MixTravelRouteSeed(toMap + 0x27d4eb2fu);
        seed ^= MixTravelRouteSeed(quantize(toX) + 0x165667b1u);
        seed ^= MixTravelRouteSeed(quantize(toY) + 0xd3a2646cu);

        return 1.0f + 0.08f * static_cast<float>(seed % 1024u) / 1023.0f;
    }
}
