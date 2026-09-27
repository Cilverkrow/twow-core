#ifndef TW_FUNSERVER_ROGUE_TALENTS_H
#define TW_FUNSERVER_ROGUE_TALENTS_H

#include <cstdint>
#include <initializer_list>

// Issue twow-repo#367: the owner's rogue talent line (2026-09-27) as bot-only auras
// 90150-90193, granted by ClassGrant (OB-10). Pure rules only, so they can be tested
// without a server; the core hooks read the aura amounts and call these.

enum FunserverRogueTalentSpell : uint32_t
{
    ROGUE_TALENT_BEHIND_R1            = 90150,  // Assassination R1/C4, +5 %/rank from behind
    ROGUE_TALENT_BEHIND_R4            = 90153,
    ROGUE_TALENT_COOLDOWN_FLOW_R1     = 90154,  // Assassination R3/C4
    ROGUE_TALENT_COOLDOWN_FLOW_R2     = 90155,
    ROGUE_TALENT_COLD_BLOOD           = 90156,  // Assassination R5/C4, +30 %
    ROGUE_TALENT_VIGOR_FURY           = 90157,  // Assassination R7/C1
    ROGUE_TALENT_SEAL_FATE_ECHO       = 90158,  // Assassination R7/C3, 33 %
    ROGUE_TALENT_RIPOSTE_FLOW_R1      = 90169,  // Combat R2/C4
    ROGUE_TALENT_RIPOSTE_FLOW_R3      = 90171,
    ROGUE_TALENT_ARCANE_EVASION_R1    = 90172,  // Combat R4/C4
    ROGUE_TALENT_ARCANE_EVASION_R3    = 90174,
    ROGUE_TALENT_EXECUTE_R1           = 90175,  // Combat R6/C1, +10/20/30 % below 35 %
    ROGUE_TALENT_EXECUTE_R3           = 90177,
    ROGUE_TALENT_FRONTAL_BACKSTAB     = 90178,  // Combat R7/C1, below 60 %
    ROGUE_TALENT_GHOSTLY_EVASION      = 90182,  // Combat R7/C4
    ROGUE_TALENT_SHADOW_R1            = 90183,  // Subtlety R1/C1, +5 %/rank in stealth
    ROGUE_TALENT_SHADOW_R4            = 90186,
    ROGUE_TALENT_HEMORRHAGE_STACKS    = 90187,  // Subtlety R6/C4
    ROGUE_TALENT_SHADOW_EDGE_R1       = 90188,  // Subtlety R7/C3, +8/16/24 % as Shadow
    ROGUE_TALENT_SHADOW_EDGE_R3       = 90190,
    ROGUE_TALENT_VIGOR_FURY_BUFF      = 90191,  // helper: +2 % damage, 8 s, 10 stacks
    ROGUE_TALENT_SHADOW_EDGE_DAMAGE   = 90192,  // helper: the Shadow part
    ROGUE_TALENT_HEMORRHAGE_STACK     = 90193,  // helper: extra Hemorrhage stacks

    SPELL_ROGUE_COLD_BLOOD_FUNSERVER  = 14177,  // existing Cold Blood (next ability crits)
    SPELL_ROGUE_GHOSTLY_STRIKE_FUNSERVER = 14278, // existing Ghostly Strike (dodge buff on self)
};

uint32_t constexpr ROGUE_TALENT_EXECUTE_HEALTH_PCT = 35;

// Percent damage bonuses stack multiplicatively, like the core's other done-percent mods.
inline float FunserverRogueTalentDamageMultiplier(int32_t behindPct, int32_t stealthPct, int32_t executePct, int32_t coldBloodPct)
{
    float multiplier = 1.0f;
    for (int32_t pct : { behindPct, stealthPct, executePct, coldBloodPct })
        if (pct > 0)
            multiplier *= (100.0f + float(pct)) / 100.0f;
    return multiplier;
}

inline bool FunserverRogueExecuteApplies(float victimHealthPct)
{
    return victimHealthPct < float(ROGUE_TALENT_EXECUTE_HEALTH_PCT);
}

// Combat R7/C1: Backstab from the front while the target is below the talent's threshold.
inline bool FunserverRogueFrontalBackstabAllowed(float victimHealthPct, int32_t thresholdPct)
{
    return thresholdPct > 0 && victimHealthPct < float(thresholdPct);
}

// Combat R4/C4 (owner decision 2026-09-27, #367): real resistance in all magic schools,
// no cap. The rank converts sharePct (15/30/45 %) of dodge % + parry %, and every
// resulting percent gives pointsPerPct (1/2/3) resistance. 30 + 20 at rank 3: 22.5 x 3 = 67.
inline int32_t FunserverRogueArcaneEvasionResistance(float dodgePct, float parryPct, int32_t sharePct, int32_t pointsPerPct)
{
    if (sharePct <= 0 || pointsPerPct <= 0)
        return 0;
    float const avoidance = (dodgePct > 0.0f ? dodgePct : 0.0f) + (parryPct > 0.0f ? parryPct : 0.0f);
    return int32_t(avoidance * float(sharePct) / 100.0f * float(pointsPerPct));
}

#endif
