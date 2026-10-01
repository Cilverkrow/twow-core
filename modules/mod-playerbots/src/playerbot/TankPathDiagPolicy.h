#pragma once

#include <cstdint>

namespace ai::tank_path_diag
{
// Hotfix 8.1 (OB-00 2026-09-30, owner variant B: 1 rogue + 1 shaman tank per
// faction on 4.3 / 7.3): does a bot on a tank path actually tank? Diagnostic
// only. A bot with "tank rogue" / "tank shaman" logs [TankPath] state=on once
// per session and, while it is in a group or an instance, one summary per ten
// minutes (at most 6 lines per bot and hour).
constexpr uint32_t SummarySeconds = 600;
constexpr uint32_t SampleSeconds = 1;

struct Window
{
    uint32_t start = 0;
    uint32_t pulls = 0;
    uint32_t combatSamples = 0;
    uint32_t aggroSamples = 0;
    uint32_t taunts = 0;
    uint32_t deaths = 0;
    bool inCombat = false;

    // One sample per SampleSeconds: entering combat counts a pull; while in
    // combat, holding aggro means at least one attacker has the bot as victim.
    void Sample(bool combat, bool holdsAggro)
    {
        if (combat && !inCombat)
            ++pulls;
        inCombat = combat;
        if (!combat)
            return;
        ++combatSamples;
        if (holdsAggro)
            ++aggroSamples;
    }

    uint32_t AggroPercent() const
    {
        return combatSamples ? uint32_t(uint64_t(aggroSamples) * 100 / combatSamples) : 0;
    }

    bool Due(uint32_t now) const
    {
        return start && now - start >= SummarySeconds;
    }

    // A new window; the combat state carries over so a running fight is no new pull.
    void Restart(uint32_t now)
    {
        bool const combat = inCombat;
        *this = Window();
        start = now;
        inCombat = combat;
    }
};
}
