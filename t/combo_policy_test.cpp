// twow-repo#484 hotfix 8.25: owner rules for rogue combo points (FunserverComboPolicy.h).
// Negative probes: with both switches off the upstream behaviour is unchanged.
#include "../src/game/FunserverComboPolicy.h"

#include <iostream>

namespace
{
    int failures = 0;

    void Check(bool ok, char const* label)
    {
        if (!ok)
        {
            std::cerr << "FAIL: " << label << "\n";
            ++failures;
        }
    }

    ComboProcRedirectInput SetupOnSecondAttacker()
    {
        // Rogue with 3 points on its target dodges a second attacker; Setup procs on that one.
        ComboProcRedirectInput in;
        in.switchOn = true;
        in.fromProc = true;
        in.isRogue = true;
        in.procTargetIsComboTarget = false;
        in.comboPoints = 3;
        in.comboTargetValid = true;
        in.selectionValid = true;
        in.selectionIsProcTarget = false;
        return in;
    }
}

int main()
{
    // --- Rogue.KeepComboPointsOnSelect -------------------------------------------------
    // Switch off = upstream: a rogue/druid selecting another existing unit drops the points.
    Check(ShouldClearComboPointsOnSelect(false, true, true, false), "off: select other unit drops points");
    Check(!ShouldClearComboPointsOnSelect(false, true, true, true), "off: reselecting the combo target keeps points");
    Check(!ShouldClearComboPointsOnSelect(false, true, false, false), "off: clearing the selection keeps points");
    Check(!ShouldClearComboPointsOnSelect(false, false, true, false), "off: warriors and others never drop on select");
    // Switch on: never dropped by selecting.
    Check(!ShouldClearComboPointsOnSelect(true, true, true, false), "on: select other unit keeps points");
    Check(!ShouldClearComboPointsOnSelect(true, false, true, false), "on: other classes unchanged");

    // --- Rogue.ProcComboPointsToCurrentTarget -------------------------------------------
    ComboProcRedirectInput in = SetupOnSecondAttacker();
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::ComboTarget, "on: Setup point goes to the combo target");

    in = SetupOnSecondAttacker();
    in.switchOn = false;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::None, "off: standard behaviour (points move)");

    in = SetupOnSecondAttacker();
    in.fromProc = false;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::None, "on: own combo builders are not redirected");

    in = SetupOnSecondAttacker();
    in.isRogue = false;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::None, "on: only rogues");

    in = SetupOnSecondAttacker();
    in.procTargetIsComboTarget = true;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::None, "on: proc on the combo target is normal");

    in = SetupOnSecondAttacker();
    in.comboPoints = 0;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::Selection, "on: no points -> the selected target");

    in = SetupOnSecondAttacker();
    in.comboTargetValid = false;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::Selection, "on: dead combo target -> the selected target");

    in = SetupOnSecondAttacker();
    in.comboPoints = 0;
    in.selectionIsProcTarget = true;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::None, "on: selection is the attacker -> normal");

    in = SetupOnSecondAttacker();
    in.comboTargetValid = false;
    in.selectionValid = false;
    Check(DecideComboProcRedirect(in) == ComboProcRedirect::None, "on: no valid current target -> standard behaviour");

    if (failures)
    {
        std::cerr << failures << " combo policy check(s) failed\n";
        return 1;
    }
    std::cout << "COMBO_POLICY_TEST=PASS\n";
    return 0;
}
