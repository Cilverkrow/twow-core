// twow-repo#295 owner decisions 2026-10-02: riding in four stages, mount speed by rank and
// family, stronger slows and longer roots of player-controlled casters.
#include "../src/game/FunserverRidingStages.h"

#include <cstdint>
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

    void ExpectSpeed(bool stages, uint32_t skill, uint32_t level, int32_t baseSpeed, bool speed100, int32_t expected)
    {
        int32_t const actual = FunserverRiding::MountedSpeedPct(stages, skill, level, baseSpeed, speed100);
        if (actual != expected)
        {
            std::cerr << "FAIL: MountedSpeedPct(stages=" << stages << ", skill=" << skill << ", level=" << level
                      << ", base=" << baseSpeed << ", speed100=" << speed100 << ") = " << actual
                      << ", expected " << expected << "\n";
            ++failures;
        }
    }

    void ExpectSlow(int32_t amount, uint32_t slowPct, uint32_t maxSlowPct, int32_t expected, char const* label)
    {
        int32_t const actual = FunserverSnare::ScaleSlow(amount, slowPct, maxSlowPct);
        if (actual != expected)
        {
            std::cerr << "FAIL: " << label << ": ScaleSlow(" << amount << ", " << slowPct << ", " << maxSlowPct
                      << ") = " << actual << ", expected " << expected << "\n";
            ++failures;
        }
    }

    void ExpectRoot(int32_t durationMs, uint32_t rootPct, int32_t expected, char const* label)
    {
        int32_t const actual = FunserverSnare::ScaleRootDuration(durationMs, rootPct);
        if (actual != expected)
        {
            std::cerr << "FAIL: " << label << ": ScaleRootDuration(" << durationMs << ", " << rootPct
                      << ") = " << actual << ", expected " << expected << "\n";
            ++failures;
        }
    }

    // Mount kinds of the speed tables below: a slow mount (spell speed +60), a swift mount
    // (+100), a SPELL_CUSTOM_MOUNT_SPEED_100 mount with a swift and with a slow spell value.
    struct MountKind
    {
        int32_t baseSpeed;
        bool speed100;
    };
    MountKind const SLOW = { 60, false };
    MountKind const SWIFT = { 100, false };
    MountKind const FLAG_SWIFT = { 100, true };
    MountKind const FLAG_SLOW = { 60, true };
    MountKind const KINDS[] = { SLOW, SWIFT, FLAG_SWIFT, FLAG_SLOW };

    uint32_t const SKILLS[] = { 0, 74, 75, 149, 150, 224, 225, 299, 300 };
    uint32_t const LEVELS[] = { 60, 23 };
    int32_t const HALF_LEVEL = -1;  // marker: the expected value is level / 2

    // Expected bonus per skill (rows) and mount kind (columns: slow, swift, flag swift, flag slow).
    int32_t const STAGES_ON[9][4] = {
        { HALF_LEVEL, HALF_LEVEL, 100, 100 },   //   0
        { HALF_LEVEL, HALF_LEVEL, 100, 100 },   //  74
        {  60,  60, 100, 100 },                 //  75 rank 1
        {  60,  60, 100, 100 },                 // 149
        { 100, 100, 100, 100 },                 // 150 rank 2
        { 100, 100, 100, 100 },                 // 224
        { 100, 140, 140, 140 },                 // 225 rank 3
        { 100, 140, 140, 140 },                 // 299
        { 100, 180, 180, 180 },                 // 300 rank 4
    };
    // Switch off: the inherited values (75 -> 60, 150 and above -> 100), never an unmount.
    int32_t const STAGES_OFF[9][4] = {
        { HALF_LEVEL, HALF_LEVEL, 100, 100 },   //   0
        { HALF_LEVEL, HALF_LEVEL, 100, 100 },   //  74
        {  60,  60, 100, 100 },                 //  75
        {  60,  60, 100, 100 },                 // 149
        { 100, 100, 100, 100 },                 // 150
        { 100, 100, 100, 100 },                 // 224
        { 100, 100, 100, 100 },                 // 225
        { 100, 100, 100, 100 },                 // 299
        { 100, 100, 100, 100 },                 // 300
    };
}

int main()
{
    using namespace FunserverRiding;

    // Every skill boundary x mount kind x switch, at two levels for the level / 2 rule.
    for (uint32_t level : LEVELS)
    {
        for (uint32_t s = 0; s < 9; ++s)
        {
            for (uint32_t k = 0; k < 4; ++k)
            {
                int32_t on = STAGES_ON[s][k];
                int32_t off = STAGES_OFF[s][k];
                if (on == HALF_LEVEL)
                    on = static_cast<int32_t>(level / 2);
                if (off == HALF_LEVEL)
                    off = static_cast<int32_t>(level / 2);
                ExpectSpeed(true, SKILLS[s], level, KINDS[k].baseSpeed, KINDS[k].speed100, on);
                ExpectSpeed(false, SKILLS[s], level, KINDS[k].baseSpeed, KINDS[k].speed100, off);
            }
        }
    }

    // Rank 0 keeps the inherited level / 2 rule (integer division), both switch states.
    ExpectSpeed(true, 0, 1, 60, false, 0);
    ExpectSpeed(true, 0, 2, 100, false, 1);
    ExpectSpeed(true, 0, 59, 60, false, 29);
    ExpectSpeed(false, 0, 59, 100, false, 29);
    ExpectSpeed(false, 0, 60, 60, false, 30);
    ExpectSpeed(true, 0, 0, 60, false, 0);

    // Family from the mount spell's own aura-32 value: below 100 slow, 100 and above swift.
    Check(FamilyOf(99, false) == MountFamily::Slow, "spell speed 99 is family 1");
    Check(FamilyOf(100, false) == MountFamily::Swift, "spell speed 100 is family 2");
    Check(FamilyOf(60, false) == MountFamily::Slow, "spell speed 60 is family 1");
    Check(FamilyOf(0, false) == MountFamily::Slow, "tallstrider (0) is family 1");
    Check(FamilyOf(40, false) == MountFamily::Slow, "rented mount (40) is family 1");
    Check(FamilyOf(150, false) == MountFamily::Swift, "racing car value (150) is family 2");
    Check(FamilyOf(60, true) == MountFamily::Swift, "SPEED_100 flag is family 2");
    ExpectSpeed(true, 300, 60, 99, false, 100);    // family 1 cap
    ExpectSpeed(true, 300, 60, 0, false, 100);     // tallstrider follows the rank
    ExpectSpeed(true, 225, 60, 40, false, 100);    // rented mount follows the rank
    ExpectSpeed(true, 300, 60, 150, false, 180);

    // Rank from the pure skill value.
    Check(RankFromSkill(0) == 0 && RankFromSkill(74) == 0, "below 75 is rank 0");
    Check(RankFromSkill(75) == 1 && RankFromSkill(149) == 1, "75..149 is rank 1");
    Check(RankFromSkill(150) == 2 && RankFromSkill(224) == 2, "150..224 is rank 2");
    Check(RankFromSkill(225) == 3 && RankFromSkill(299) == 3, "225..299 is rank 3");
    Check(RankFromSkill(300) == 4 && RankFromSkill(375) == 4, "300 and above is rank 4");

    // Training levels 10/20/40/60.
    Check(MaxRankForLevel(1) == 0 && MaxRankForLevel(9) == 0, "no riding below level 10");
    Check(MaxRankForLevel(10) == 1 && MaxRankForLevel(19) == 1, "rank 1 from level 10");
    Check(MaxRankForLevel(20) == 2 && MaxRankForLevel(39) == 2, "rank 2 from level 20");
    Check(MaxRankForLevel(40) == 3 && MaxRankForLevel(59) == 3, "rank 3 from level 40");
    Check(MaxRankForLevel(60) == 4 && MaxRankForLevel(61) == 4, "rank 4 from level 60");

    // Owner values: skills, levels, training 50 s / 5 g / 50 g / 500 g, mounts 1 g / 100 g.
    Check(SKILL_ID == 762, "riding skill 762");
    Check(RANK_SKILL[0] == 75 && RANK_SKILL[1] == 150 && RANK_SKILL[2] == 225 && RANK_SKILL[3] == 300, "rank skills");
    Check(RANK_LEVEL[0] == 10 && RANK_LEVEL[1] == 20 && RANK_LEVEL[2] == 40 && RANK_LEVEL[3] == 60, "rank levels");
    Check(RANK_TRAINING_COST_COPPER[0] == 5000 && RANK_TRAINING_COST_COPPER[1] == 50000 &&
          RANK_TRAINING_COST_COPPER[2] == 500000 && RANK_TRAINING_COST_COPPER[3] == 5000000, "training costs");
    Check(MOUNT1_PRICE_COPPER == 10000 && MOUNT2_PRICE_COPPER == 1000000, "mount prices 1 g / 100 g");
    Check(FAMILY1_REQUIRED_SKILL == 75 && FAMILY2_REQUIRED_SKILL == 225, "mount riding requirements");

    // Live acceptance speeds (base run speed 7.0 y/s, in tenths): 11.2 / 14.0 / 16.8 / 19.6.
    Check(70 * (100 + MountedSpeedPct(true, 75, 10, 60, false)) / 100 == 112, "family 1 at stage 10: 11.2 y/s");
    Check(70 * (100 + MountedSpeedPct(true, 150, 20, 60, false)) / 100 == 140, "family 1 at stage 20: 14.0 y/s");
    Check(70 * (100 + MountedSpeedPct(true, 225, 40, 100, false)) / 100 == 168, "family 2 at stage 40: 16.8 y/s");
    Check(70 * (100 + MountedSpeedPct(true, 300, 60, 100, false)) / 100 == 196, "family 2 at stage 60: 19.6 y/s");

    // Slows x1.4, capped at 90 %, never weaker than the original.
    ExpectSlow(-50, 40, 90, -70, "Hamstring 50 % -> 70 %");
    ExpectSlow(-30, 40, 90, -42, "Curse of Exhaustion 30 % -> 42 %");
    ExpectSlow(-40, 40, 90, -56, "Frostbolt 40 % -> 56 %");
    ExpectSlow(-60, 40, 90, -84, "Frost Trap 60 % -> 84 %");
    ExpectSlow(-70, 40, 90, -90, "70 % capped at 90 %");
    ExpectSlow(-90, 40, 90, -90, "90 % stays at the cap");
    ExpectSlow(-95, 40, 90, -95, "stronger than the cap stays");
    ExpectSlow(-1, 40, 90, -1, "integer rounding toward zero");
    ExpectSlow(-50, 0, 90, -50, "0 % keeps the slow");
    ExpectSlow(50, 40, 90, 50, "positive amount stays");
    ExpectSlow(0, 40, 90, 0, "zero stays");
    ExpectSlow(-50, 40, 0, -50, "cap 0 never weakens a slow");
    ExpectSlow(-50, 100, 90, -90, "x2 capped at 90 %");
    ExpectSlow(-50, 40, 150, -70, "cap above 100 counts as 100");
    Check(FunserverSnare::DEFAULT_SLOW_PCT == 0 && FunserverSnare::DEFAULT_MAX_SLOW_PCT == 90 &&
          FunserverSnare::DEFAULT_ROOT_DURATION_PCT == 0, "neutral snare defaults");

    // Roots last 40 % longer; instant and permanent durations stay.
    ExpectRoot(8000, 40, 11200, "Frost Nova 8 s -> 11.2 s");
    ExpectRoot(5000, 40, 7000, "Improved Hamstring 5 s -> 7 s");
    ExpectRoot(27000, 40, 37800, "Entangling Roots 27 s -> 37.8 s");
    ExpectRoot(-1, 40, -1, "permanent stays");
    ExpectRoot(0, 40, 0, "instant stays");
    ExpectRoot(8000, 0, 8000, "0 % keeps the duration");
    ExpectRoot(10000, 200, 30000, "200 % triples");
    ExpectRoot(INT32_MAX, 200, INT32_MAX, "overflow-safe");

    if (failures)
        return 1;
    std::cout << "RIDING_STAGES_POLICY=PASS\n";
    return 0;
}
