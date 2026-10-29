#pragma once

// twow-repo#575 F9-2 (CAUSE.md 3b): a revive ends the ghost, so a graveyard repop still scheduled for it is stale.
// Player::ScheduleRepopAtGraveyard (the repop request, the auto release in Player::Update, the BG insignia) only sets
// m_repopAtGraveyardPending; Player::Update runs RepopAtGraveyard() once no movement change is pending. A player revived
// before that (a bot's death-loop escape one second after its auto release, a resurrection accepted right after the
// release) was then moved alive to the graveyard nearest to wherever it stood. Header-only, no core dependency (unit test).
namespace repop
{
    // Clears the pending repop; true = one was pending (counted for the [Repop] minute line).
    inline bool ClearStale(bool& pending)
    {
        bool const stale = pending;
        pending = false;
        return stale;
    }
}
