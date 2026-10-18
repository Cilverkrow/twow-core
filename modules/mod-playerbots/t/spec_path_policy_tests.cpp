// twow-repo#602 ROT-01 R06a (AiPlayerbot.Rotation.SpecFromPath): the spec tab of a bot on a premade path from level 10,
// the role tree of a path, the load check and the [SpecPath] lines (SpecPathPolicy.h).
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "SpecPathPolicy.h"
#include "CohortPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

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
}

int main()
{
    using namespace ai::spec_path;

    // 1) Valid tabs.
    {
        Require(ValidTab(0) == 0 && ValidTab(1) == 1 && ValidTab(2) == 2, "tabs 0..2");
        Require(ValidTab(-1) == -1 && ValidTab(3) == -1 && ValidTab(-7) == -1, "anything else is not given");
    }

    // 2) The path tab wins only when the switch applies, from level 10, with a valid tab.
    {
        Require(PathTabFor(true, 60, 2) == 2, "shadow priest at 60 stays shadow (tab 2), whatever the points say");
        Require(PathTabFor(true, 10, 0) == 0, "from level 10");
        Require(PathTabFor(true, 9, 2) == -1, "below level 10: the class default (majority) stays");
        Require(PathTabFor(false, 60, 2) == -1, "switch off / other cohort: the majority");
        Require(PathTabFor(true, 60, -1) == -1 && PathTabFor(true, 60, 3) == -1, "no path or no tab key: the majority");
    }

    // 3) Cohort mode wiring as AiFactory::GetPlayerSpecTab does it.
    {
        auto tabFor = [](std::uint32_t mode, std::uint32_t guid) { return PathTabFor(ai::cohort::Applies(mode, guid), 60, 2); };
        Require(tabFor(0, 1) == -1 && tabFor(0, 2) == -1, "mode 0: nobody");
        Require(tabFor(1, 1) == 2 && tabFor(1, 2) == 2, "mode 1: every bot");
        Require(tabFor(2, 1) == 2 && tabFor(2, 2) == -1, "mode 2: only cohort B (odd guid)");
        Require(tabFor(7, 1) == -1, "unknown mode acts like 0");
    }

    // 4) Role tree of a path: the key, else the old rule (main tree of the last link).
    {
        Require(PathRoleTab(2, 0) == 2, "shadow 5.1 (L60 link 49/4/49 -> tree 0) is tree 2 by its key");
        Require(PathRoleTab(-1, 0) == 0, "old config without key: last link tree as before");
        Require(PathRoleTab(5, 1) == 1, "invalid key: last link tree");
    }

    // 5) Load check.
    {
        Require(TabAgreesWithFirstLink(1, 1), "key and level 10 link agree");
        Require(!TabAgreesWithFirstLink(1, 0), "mismatch");
        Require(TabAgreesWithFirstLink(-1, 0), "no key: nothing to check");
    }

    // 6) Lines.
    {
        Require(FormatLoadedLine(30, 30, 0) == "[SpecPath] loaded paths=30 with_tab=30 mismatches=0", "loaded line");
        Require(FormatMismatchLine(5, "shadow", 2, 0) ==
            "[SpecPath] mismatch class=5 path=shadow tab=2 first_link_tree=0 (the key wins)", "mismatch line");
        std::string const longName(100, 'n');
        Require(FormatMismatchLine(5, longName, 2, 0).find(std::string(40, 'n') + " tab=2") != std::string::npos, "name cut to 40");
    }

    std::cout << "spec_path_policy_tests passed\n";
    return 0;
}
