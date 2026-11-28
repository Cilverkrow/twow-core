#pragma once

#include "CohortPolicy.h"
#include "ProfessionPair.h"

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// twow-repo#333 Berufe GrantOnInit (owner 27.11.2026 via OB-00: roster bots get their professions at start; owner
// economy decision 28.11.2026 via OB-00: the starter tools are given, verbatim "schenken"). A persistent roster bot
// that lacks one of the two primaries of its persisted profession_pair learns the apprentice rank the way the bots'
// trainer path does (TrainerAction::Learn) - same core state check, same learnSpell of the taught spell - without a
// trainer visit and without money. Key AiPlayerbot.ProfessionTraining.GrantOnInit, 0 off (default), 1 all,
// 2 cohort B (odd low guid), 3 cohort A (even low guid).
namespace ai::profession_grant
{
// Mode values above 3 are off (the loader writes one error line).
inline std::uint32_t ClampMode(std::uint32_t mode)
{
    return mode > cohort::ModeOnlyA ? cohort::ModeOff : mode;
}

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return cohort::Applies(ClampMode(mode), guidLow);
}

// The starter tools given together with a granted apprentice rank (owner economy decision 28.11.2026 via OB-00).
// Only the tools the recipes / gathering up to skill 75 need (OB-40 r-recipe-tools-to75.tsv 54e8be01): Mining
// Pick, Skinning Knife, Blacksmith Hammer, Arclight Spanner, Runed Copper Rod, Jewelers Kit (Turtle 55155).
// Herbalism, Alchemy, Leatherworking and Tailoring need none. 0 = no item.
constexpr std::uint32_t kMiningPick = 2901;
constexpr std::uint32_t kSkinningKnife = 7005;
constexpr std::uint32_t kBlacksmithHammer = 5956;
constexpr std::uint32_t kArclightSpanner = 6219;
constexpr std::uint32_t kRunedCopperRod = 6218;
constexpr std::uint32_t kJewelersKit = 55155;

inline std::array<std::uint32_t, 2> StarterTools(std::uint32_t skill)
{
    switch (skill)
    {
    case profession::kMining:         return { kMiningPick, 0 };
    case profession::kSkinning:       return { kSkinningKnife, 0 };
    case profession::kBlacksmithing:  return { kBlacksmithHammer, 0 };
    case profession::kEngineering:    return { kBlacksmithHammer, kArclightSpanner };
    case profession::kEnchanting:     return { kRunedCopperRod, 0 };
    case profession::kJewelcrafting:  return { kJewelersKit, 0 };
    default:                          return { 0, 0 };
    }
}

inline bool IsPrimary(std::uint32_t skill)
{
    switch (skill)
    {
    case profession::kAlchemy: case profession::kBlacksmithing: case profession::kEnchanting:
    case profession::kEngineering: case profession::kHerbalism: case profession::kJewelcrafting:
    case profession::kLeatherworking: case profession::kMining: case profession::kSkinning:
    case profession::kTailoring:
        return true;
    default:
        return false;
    }
}

// A trainer spell is an apprentice candidate when it needs no skill yet and the spell it teaches is the first
// rank of a primary profession (the core's IsPrimaryProfessionFirstRankSpell).
inline bool IsApprenticeCandidate(std::uint32_t reqSkill, bool teachesFirstRank, std::uint32_t skill)
{
    return reqSkill == 0 && teachesFirstRank && IsPrimary(skill);
}

// The skill -> apprentice teaching spell table, built once from every tradeskill trainer list (npc_trainer and
// npc_trainer_template). A skill whose trainers name more than one distinct apprentice teaching spell is
// ambiguous: no grant for it (kAmbiguous) and an error line, never a guess.
constexpr std::uint32_t kAmbiguous = 0xFFFFFFFFu;

inline std::map<std::uint32_t, std::uint32_t> ResolveApprentice(
    std::vector<std::pair<std::uint32_t, std::uint32_t>> const& skillSpells)
{
    std::map<std::uint32_t, std::uint32_t> table;
    for (auto const& [skill, spell] : skillSpells)
    {
        if (!IsPrimary(skill) || !spell)
            continue;
        auto const it = table.find(skill);
        if (it == table.end())
            table[skill] = spell;
        else if (it->second != spell)
            it->second = kAmbiguous;
    }
    return table;
}

// What the grant did for one planned skill. Text for the [ProfessionGrant] result= field.
enum class Result : std::uint8_t
{
    Ok,                 // learned; the skill is there afterwards
    LearnFailed,        // learnSpell ran, the skill is still missing
    NoApprenticeSpell,  // no trainer list has an apprentice spell for this skill
    AmbiguousSpell,     // more than one distinct apprentice spell for this skill
    NotGreen,           // the core state check refused (level, race / class, primary limit ...)
};

inline char const* ToString(Result result)
{
    switch (result)
    {
    case Result::Ok:                return "ok";
    case Result::LearnFailed:       return "learn_failed";
    case Result::NoApprenticeSpell: return "no_apprentice_spell";
    case Result::AmbiguousSpell:    return "ambiguous_spell";
    case Result::NotGreen:          return "not_green";
    }
    return "unknown";
}

// A refused state is retried on later checks (a level-gated rank such as Jewelcrafting at level 5 becomes GREEN
// on level-up); everything else is final until the next login, so the periodic check never repeats a failure.
inline bool RetryOnTick(Result result)
{
    return result == Result::NotGreen;
}

// Only the login check writes a not_green line; the periodic check writes a line only when it grants or fails.
inline bool ShouldLog(Result result, bool login)
{
    return login || result != Result::NotGreen;
}
}
