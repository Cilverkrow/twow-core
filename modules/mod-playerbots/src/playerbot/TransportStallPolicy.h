#pragma once

#include <cstdint>

namespace ai::transport_stall
{
// Hotfix 8.13 (twow-repo#497, bot 65 Talaster): a quest route to another continent ended on the
// zeppelin platform without boarding. The turn-in recovery never got past stage 1 because every
// short gathering trip from the platform counted as progress. For a target on another map only a
// shorter distance to the target counts (the distance runs through the map transfer, so walking to
// the dock is progress, waiting on the platform is not). After 12 minutes without a better distance
// the target is dropped and suppressed for 30 minutes.
constexpr uint32_t WaitLogMs = 5 * 60 * 1000;
constexpr uint32_t StallMs = 12 * 60 * 1000;
constexpr uint32_t CooldownMs = 30 * 60 * 1000;
constexpr float MinimumProgressDistance = 25.0f;

enum class Step : uint8_t
{
    None,
    Wait,       // log line, every WaitLogMs without a better distance
    Abandon     // drop the target and suppress it for CooldownMs
};

struct State
{
    uint32_t entry = 0;
    uint32_t questId = 0;
    float bestDistance = 0.0f;
    uint32_t bestAt = 0;        // 0 = not observing
    uint32_t loggedAt = 0;
    uint32_t minutes = 0;       // waited minutes at the last Wait or Abandon

    // paused: combat, death, taxi, aboard a transport, real master (same as the turn-in recovery).
    Step Observe(uint32_t targetEntry, uint32_t targetQuest, bool crossMap, bool paused, float distance, uint32_t now)
    {
        if (!crossMap)
        {
            bestAt = 0;
            return Step::None;
        }
        if (!bestAt || entry != targetEntry || questId != targetQuest || paused ||
            distance + MinimumProgressDistance <= bestDistance)
        {
            entry = targetEntry;
            questId = targetQuest;
            bestDistance = distance;
            bestAt = now ? now : 1;
            loggedAt = bestAt;
            return Step::None;
        }
        minutes = (now - bestAt) / 60000;
        if (now - bestAt >= StallMs)
        {
            bestAt = 0;
            return Step::Abandon;
        }
        if (now - loggedAt >= WaitLogMs)
        {
            loggedAt = now;
            return Step::Wait;
        }
        return Step::None;
    }
};
}
