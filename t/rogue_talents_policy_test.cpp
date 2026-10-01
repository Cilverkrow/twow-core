// twow-repo#367 rules for the owner's rogue talent line (bot auras).
#include "../src/game/FunserverRogueTalents.h"

#include <cmath>
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

    bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}

int main()
{
    // Damage bonuses multiply; missing talents count as 0.
    Check(Near(FunserverRogueTalentDamageMultiplier(0, 0, 0, 0), 1.0f), "no talent, no bonus");
    Check(Near(FunserverRogueTalentDamageMultiplier(20, 0, 0, 0), 1.2f), "behind 4/4 = +20 %");
    Check(Near(FunserverRogueTalentDamageMultiplier(20, 20, 0, 0), 1.44f), "behind and stealth multiply");
    Check(Near(FunserverRogueTalentDamageMultiplier(0, 0, 30, 30), 1.69f), "execute 3/3 and Cold Blood");

    // Execute below 35 %, frontal Backstab below the talent threshold (60 %).
    Check(FunserverRogueExecuteApplies(34.9f) && !FunserverRogueExecuteApplies(35.0f), "execute threshold 35 %");
    Check(FunserverRogueFrontalBackstabAllowed(59.0f, 60) && !FunserverRogueFrontalBackstabAllowed(60.0f, 60), "frontal Backstab below 60 %");
    Check(!FunserverRogueFrontalBackstabAllowed(10.0f, 0), "no talent, no frontal Backstab");

    // Owner 2026-09-27: 30 % dodge + 20 % parry, rank 3 (45 %, 3 per percent) = 67 resistance, no cap.
    Check(FunserverRogueArcaneEvasionResistance(30.0f, 20.0f, 45, 3) == 67, "owner example rank 3");
    Check(FunserverRogueArcaneEvasionResistance(30.0f, 20.0f, 15, 1) == 7, "rank 1 = 7.5 -> 7");
    Check(FunserverRogueArcaneEvasionResistance(80.0f, 40.0f, 45, 3) == 162, "no cap");
    Check(FunserverRogueArcaneEvasionResistance(30.0f, 20.0f, 0, 3) == 0, "no talent, no resistance");

    // Hotfix 8.6: [GhostlyEvasion] / [RogueTalentTrace] windows.
    FunserverTraceWindow window;
    Check(!window.Due(100), "no event, no line");
    window.Add(true, 100);
    Check(!window.Due(100 + 3599) && window.Due(100 + 3600), "a line after one hour");
    for (int i = 0; i < 19; ++i)
        window.Add(false, 200);
    Check(window.events == 20 && window.hits == 1 && window.Due(200), "the first line after 20 events");
    window.Reset();
    Check(window.events == 0 && !window.Due(5000), "reset");
    for (int i = 0; i < 25; ++i)
        window.Add(true, 6000);
    Check(!window.Due(6000) && window.Due(6000 + 3600), "after the first line only hourly");

    if (failures)
        return 1;
    std::cout << "ROGUE_TALENTS_POLICY=PASS\n";
    return 0;
}
