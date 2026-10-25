#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ai::contested_objective
{
// twow-repo#601 F5b (CLI-Deep 25.10.2026, OB-00 go; analysis evidence/ws-10/cli-quest-organic/f5-suppression/F5-REPORT.md):
// in the QFO candidate 59 % of the quest-objective work timeouts (403 suppressions in cohort B, 16 in A) and 56 % of the quest-loop
// suppressions fell on objectives that >= 5 bots worked at once (GOs 3640 / 3685 / 1731 / 147557). After QuestWorkTimeouts.Max
// timeouts the objective was blocked for an hour, after QuestLoop.MaxFailures failures for 30 min - with nothing left in the zone the
// bot stood in the quest backoff. AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait: a work timeout at a contested objective
// (>= MinBots other roster bots within Radius) counts neither as a work timeout nor as a quest-loop failure; the objective is held
// back for Seconds only (about a respawn), so the next search takes another objective or quest and the objective comes back soon.
// GO objectives (loot per looter, not shareable) by default; kill objectives only with .Kill and only outside a bot-only group (the
// ad-hoc / S7 group managers share the kill credit there, CLI-GRP 25.10.).
constexpr float Radius = 40.0f;   // yards around the bot at the timeout (it stands at the objective)

// twow-repo#601 F5 (diagnostic): the death_suppressed skips of one quest search by the source of the suppression (the per-bot table of
// destination_death::Source, plus #422 gather purposes). Appended to [QuestFirstRoute] state=rejected as
// " death_suppressed_by=death:N,work:N,escape:N,park:N,contested:N,gather:N" when death_suppressed > 0; the death_suppressed total
// itself is unchanged (S1 classification, existing parsers).
constexpr std::size_t SourceGather = 5;
using SourceCounts = std::array<std::uint32_t, 6>;

inline std::string FormatSources(SourceCounts const& c)
{
    std::uint32_t sum = 0;
    for (std::uint32_t n : c)
        sum += n;
    if (!sum)
        return std::string();
    static char const* const names[] = { "death", "work", "escape", "park", "contested", "gather" };
    std::string out = " death_suppressed_by=";
    for (std::size_t i = 0; i < c.size(); ++i)
        out += std::string(i ? "," : "") + names[i] + ":" + std::to_string(c[i]);
    return out;
}

struct Facts
{
    bool on = false;               // the mode applies to this bot (CohortPolicy: 0 off, 1 all, 2 cohort B)
    bool objective = false;        // the timed-out target is a quest objective
    bool goObjective = false;      // ... at a game object (entry < 0)
    bool killToo = false;          // .Kill: creature objectives too
    bool inBotOnlyGroup = false;   // the bot is in a group of roster bots only (ad-hoc or S7)
    std::uint32_t otherBotsNear = 0;
    std::uint32_t minBots = 2;
};

inline bool Contested(Facts const& f)
{
    if (!f.on || !f.objective)
        return false;
    if (!f.goObjective && (!f.killToo || f.inBotOnlyGroup))
        return false;
    return f.otherBotsNear >= (f.minBots ? f.minBots : 1);
}
}
