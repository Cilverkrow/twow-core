#include "TrainableSpellsPrecheckPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>

// twow-repo#541 (audit A12): the skill pre-check never drops a spell the core would return GREEN, and the
// lazy roster read loads at the first GREEN spell only (switch on) or at every GREEN spell (switch off).

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

enum State
{
    Green,
    Red,
    Gray,
    GreenDisabled
};

// Model of Player::GetTrainerSpellState in the core's order of exits.
State ModelState(bool known, bool fitsClassRace, std::uint32_t spellLevel, std::uint32_t botLevel,
    bool prevMissing, bool reqMissing, std::uint32_t reqSkill, std::uint32_t reqSkillValue,
    std::uint16_t skillBase, bool isPrimaryProfLearn, bool firstRank, std::uint32_t freePoints)
{
    if (known)
        return Gray;
    if (!fitsClassRace)
        return Red;
    if (botLevel < spellLevel)
        return Red;
    if (prevMissing)
        return Red;
    if (reqMissing)
        return Red;
    if (reqSkill && skillBase < reqSkillValue)
        return Red;
    if (!isPrimaryProfLearn)
        return Green;
    if (firstRank && freePoints == 0)
        return GreenDisabled;
    return Green;
}

std::uint32_t CountLoads(State const* states, int count, bool reuse)
{
    bool alreadyRead = false;
    std::uint32_t loads = 0;
    for (int i = 0; i < count; ++i)
    {
        if (states[i] != Green)
            continue;
        if (ai::trainable_spells::ShouldReadRosterState(reuse, alreadyRead))
        {
            ++loads;
            alreadyRead = true;
        }
    }
    return loads;
}
}

int main()
{
    using ai::trainable_spells::ShouldReadRosterState;
    using ai::trainable_spells::SkillRequirementRed;

    // 1. Boundaries of the core's skill condition.
    {
        int calls = 0;
        std::uint16_t base = 0;
        auto skill = [&](std::uint32_t) -> std::uint16_t { ++calls; return base; };

        Require(!SkillRequirementRed(0, 300, skill), "no required skill is never RED");
        Require(calls == 0, "no required skill does not read the skill (short-circuit as in the core)");

        base = 0;
        Require(!SkillRequirementRed(164, 0, skill), "no skill floor is never RED");
        Require(SkillRequirementRed(164, 1, skill), "base 0 < 1 is RED");
        base = 74;
        Require(SkillRequirementRed(164, 75, skill), "base 74 < 75 is RED");
        base = 75;
        Require(!SkillRequirementRed(164, 75, skill), "equal skill is not RED");
        base = 300;
        Require(!SkillRequirementRed(164, 75, skill), "higher skill is not RED");
        base = 65535;
        Require(SkillRequirementRed(164, 70000, skill), "uint16 base vs uint32 floor promotes as in the core");
        Require(calls == 6, "the skill is read once per check with a required skill");
    }

    // 2. A spell the pre-check drops can never be GREEN, and the GREEN set is unchanged.
    {
        std::uint32_t const levels[] = { 1, 10, 60 };
        std::uint32_t const reqSkills[] = { 0, 164 };
        std::uint32_t const reqSkillValues[] = { 0, 1, 75, 300 };
        std::uint16_t const skillBases[] = { 0, 1, 74, 75, 300 };
        std::uint32_t checked = 0;
        std::uint32_t skipped = 0;
        for (int bits = 0; bits < 64; ++bits)
        {
            bool const known = bits & 1;
            bool const fitsClassRace = bits & 2;
            bool const prevMissing = bits & 4;
            bool const reqMissing = bits & 8;
            bool const isPrimaryProfLearn = bits & 16;
            bool const firstRank = bits & 32;
            for (std::uint32_t spellLevel : levels)
                for (std::uint32_t botLevel : levels)
                    for (std::uint32_t reqSkill : reqSkills)
                        for (std::uint32_t reqSkillValue : reqSkillValues)
                            for (std::uint16_t skillBase : skillBases)
                                for (std::uint32_t freePoints : { 0u, 1u })
                                {
                                    State const state = ModelState(known, fitsClassRace, spellLevel, botLevel,
                                        prevMissing, reqMissing, reqSkill, reqSkillValue, skillBase,
                                        isPrimaryProfLearn, firstRank, freePoints);
                                    bool const red = SkillRequirementRed(reqSkill, reqSkillValue,
                                        [skillBase](std::uint32_t) { return skillBase; });
                                    if (red)
                                    {
                                        ++skipped;
                                        Require(state != Green, "a pre-checked RED spell is never GREEN");
                                    }
                                    bool const keptOld = state == Green;
                                    bool const keptNew = !red && state == Green;
                                    Require(keptOld == keptNew, "same GREEN decision with and without the pre-check");
                                    ++checked;
                                }
        }
        Require(checked > 0 && skipped > 0, "the grid covers skipped and kept spells");
    }

    // 3. Lazy roster read.
    {
        State const mixed[] = { Red, Green, Gray, Green, Green };
        Require(CountLoads(mixed, 5, false) == 3, "switch off: one load per GREEN spell (old path)");
        Require(CountLoads(mixed, 5, true) == 1, "switch on: one load per calculation");
        Require(CountLoads(mixed, 1, true) == 0, "switch on: no load before the first GREEN spell");
        Require(CountLoads(mixed, 2, true) == 1, "switch on: the load happens at the first GREEN spell");

        State const none[] = { Red, Gray };
        Require(CountLoads(none, 2, false) == 0, "switch off: no GREEN spell, no load");
        Require(CountLoads(none, 2, true) == 0, "switch on: no GREEN spell, no load");

        Require(ShouldReadRosterState(false, false), "off reads");
        Require(ShouldReadRosterState(false, true), "off always re-reads");
        Require(ShouldReadRosterState(true, false), "on reads the first time");
        Require(!ShouldReadRosterState(true, true), "on reuses the first read");
    }

    std::cout << "trainable_spells_precheck_policy_tests passed\n";
    return 0;
}
