#ifndef FUNSERVER_COMBO_POLICY_H
#define FUNSERVER_COMBO_POLICY_H

#include <cstdint>

// Hotfix 8.25 (twow-repo#484): owner rules for rogue combo points, both behind a
// mangosd.conf switch with default 0 (= the behaviour before 8.25).
//
//  - Rogue.KeepComboPointsOnSelect: selecting another unit no longer drops the points
//    of a rogue or druid (CMSG_SET_SELECTION, MiscHandler.cpp). The points stay on
//    their target and are shown again when it is selected.
//  - Rogue.ProcComboPointsToCurrentTarget: a combo point that an aura-triggered spell
//    (e.g. Setup 15250 after a dodge) would put on another unit goes to the rogue's
//    current target instead (+1, max 5) and the existing points do not move. Current
//    target = the combo target that holds points, else the selection; it must be
//    alive and attackable. Without such a target nothing changes.
//
// Pure functions, unit tested in t/combo_policy_test.cpp.

// Drop combo points when the player selects `unit`? (Upstream rule plus the switch.)
inline bool ShouldClearComboPointsOnSelect(bool keepOnSelectSwitch, bool isRogueOrDruid,
    bool unitExists, bool unitIsComboTarget)
{
    if (keepOnSelectSwitch)
        return false;
    return isRogueOrDruid && unitExists && !unitIsComboTarget;
}

enum class ComboProcRedirect : uint8_t
{
    None,           // standard behaviour: the point goes to the proc's own target
    ComboTarget,    // to the combo target that already holds points
    Selection,      // to the player's selected unit
};

struct ComboProcRedirectInput
{
    bool switchOn = false;              // Rogue.ProcComboPointsToCurrentTarget
    bool fromProc = false;              // cast triggered by an aura (Setup and similar)
    bool isRogue = false;
    bool procTargetIsComboTarget = false;
    uint8_t comboPoints = 0;            // points on the combo target before the proc
    bool comboTargetValid = false;      // combo target exists, alive, attackable
    bool selectionValid = false;        // selection exists, alive, attackable
    bool selectionIsProcTarget = false;
};

inline ComboProcRedirect DecideComboProcRedirect(ComboProcRedirectInput const& in)
{
    if (!in.switchOn || !in.fromProc || !in.isRogue || in.procTargetIsComboTarget)
        return ComboProcRedirect::None;
    if (in.comboPoints > 0 && in.comboTargetValid)
        return ComboProcRedirect::ComboTarget;
    if (in.selectionValid && !in.selectionIsProcTarget)
        return ComboProcRedirect::Selection;
    return ComboProcRedirect::None;
}

#endif
