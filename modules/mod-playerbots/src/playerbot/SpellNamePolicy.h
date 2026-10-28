#pragma once

#include <array>
#include <string>

namespace ai::spell_names
{
// twow-repo#602 ROT-01 spell-name audit (OB-00 28.10.2026, bug fix, no switch): spell actions whose name never resolved through the "spell id"
// value for a bot that has learned the spell, so it was never cast. Checked against the image's spell data (sql/base spell_template +
// skill_line_ability): Barkskin (druid L44, the action said "barskin"); Turtle's Hand of Freedom / Protection / Sacrifice (paladin; the
// actions said "blessing of ..."); and the warlock stones, whose ranks are separate spells "<stone> (Minor)" .. "<stone> (Major)" - the
// bare name is only one rank, so a warlock that only knows the Minor one never created a stone. Pure rules, no game types.

constexpr char const* kBarkskin = "barkskin";
constexpr char const* kHandOfFreedom = "hand of freedom";
constexpr char const* kHandOfProtection = "hand of protection";
constexpr char const* kHandOfSacrifice = "hand of sacrifice";

// The rank names of a warlock stone, highest first ("create healthstone" -> "create healthstone (major)" .. "create healthstone (minor)").
// The action casts the first one the bot knows.
inline std::array<std::string, 5> StoneRankNames(std::string const& stone)
{
    return { stone + " (major)", stone + " (greater)", stone, stone + " (lesser)", stone + " (minor)" };
}
}
