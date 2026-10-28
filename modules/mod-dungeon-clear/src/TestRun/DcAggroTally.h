#ifndef _PLAYERBOT_DCAGGROTALLY_H
#define _PLAYERBOT_DCAGGROTALLY_H

#include <cstdint>

// CLI-GRP 28.10.2026 (OB-00 / owner: "tanks in dungeons have to perform"; G1 phase D descriptive, G2 tank comparison):
// aggro loss of a test run. The monitor samples once per step (MONITOR_STEP_MS): a combat tick is a step in which an
// alive member on the tank's map is in combat; a loss tick is a combat tick in which the tank is up and a creature is
// attacking a member other than the tank (its attacker set - the mob's victim is that member). With the tank down (dead,
// or off the map) nobody holds aggro by design: those combat ticks count as tankDownTicks, never as loss (pre-review M1).
// An event is a loss tick after a tick without loss (the tank lost a mob to the healer or a dps). Pure - no game types.
namespace DcAggro
{
    struct Tally
    {
        std::uint32_t combatTicks = 0;
        std::uint32_t lossTicks = 0;
        std::uint32_t lossEvents = 0;
        std::uint32_t tankDownTicks = 0;
        bool lastLoss = false;

        void Observe(bool inCombat, bool nonTankAttacked, bool tankUp = true)
        {
            bool const loss = inCombat && tankUp && nonTankAttacked;
            if (inCombat)
            {
                ++combatTicks;
                if (!tankUp)
                    ++tankDownTicks;
            }
            if (loss)
            {
                ++lossTicks;
                if (!lastLoss)
                    ++lossEvents;
            }
            lastLoss = loss;
        }
    };
}

#endif
