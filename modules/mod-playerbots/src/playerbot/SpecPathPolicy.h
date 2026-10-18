#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::spec_path
{
// twow-repo#602 ROT-01 R06a (AiPlayerbot.Rotation.SpecFromPath, cohort mode 0/1/2, default 0). Owner decision
// 18.10.2026: "Wir brauchen Schattenpriester und nicht Disziplin mit der Rolle Heiler, damit die auch ihr Gear
// benutzen und nicht auf Stufe 60 switchen ... Dasselbe gilt fuer alle anderen Talentbaeume." The spec identity of a
// bot on a planned premade path is the tab of the path's named tree (AiPlayerbot.PremadeSpecTab.<class>.<index>,
// written by tools/build_premade_specs.py), never the talent point majority. Below level 10 the class defaults stay
// (no talents yet; OB-00 18.10.2026). Pure rules, no game types, no locks, no config reads.
constexpr std::uint32_t MinLevel = 10;

// A talent tab is 0, 1 or 2; anything else is "not given" (-1).
inline int ValidTab(int tab)
{
    return tab >= 0 && tab <= 2 ? tab : -1;
}

// The tab AiFactory::GetPlayerSpecTab returns for a bot, or -1 = keep the point majority. `applies` = the switch
// applies to this bot (CohortPolicy.h Applies(mode, guid), only for bots); `pathTab` = the tab of the bot's
// premade path (-1 when it has none or the path has no tab key).
inline int PathTabFor(bool applies, std::uint32_t level, int pathTab)
{
    return applies && level >= MinLevel ? ValidTab(pathTab) : -1;
}

// The tree a premade path stands for in the role filter of ChangeTalentsAction: its tab key, else (old configs) the
// main tree of its last link, as before.
inline int PathRoleTab(int pathTab, int lastLinkTree)
{
    return ValidTab(pathTab) >= 0 ? pathTab : lastLinkTree;
}

// Load check: the key and the main tree of the path's first (level 10) link must agree; a path without a key is
// not checked (it keeps the point majority at runtime).
inline bool TabAgreesWithFirstLink(int pathTab, int firstLinkTree)
{
    return ValidTab(pathTab) < 0 || pathTab == firstLinkTree;
}

// "[SpecPath] loaded paths=N with_tab=M mismatches=K" once after the talent specs are loaded.
inline std::string FormatLoadedLine(std::uint32_t paths, std::uint32_t withTab, std::uint32_t mismatches)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[SpecPath] loaded paths=%u with_tab=%u mismatches=%u", paths, withTab, mismatches);
    return buf;
}

inline std::string FormatMismatchLine(std::uint32_t cls, std::string const& path, int pathTab, int firstLinkTree)
{
    char buf[192];
    std::snprintf(buf, sizeof(buf), "[SpecPath] mismatch class=%u path=%.40s tab=%d first_link_tree=%d (the key wins)",
        cls, path.c_str(), pathTab, firstLinkTree);
    return buf;
}
}
