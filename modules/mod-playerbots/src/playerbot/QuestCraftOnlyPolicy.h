#pragma once

// twow-repo#750 (AiPlayerbot.QuestFeasibility.CraftOnlySkip; OB-10 decision 840bcbdf + Nachtrag 1 be58c794, census 7226bf35): a quest whose
// required item (ReqItemId) has no source but profession recipes - no loot of any kind, no vendor, no quest reward or source
// item, no non-profession create spell - is not accepted and its giver is no trip target while the bot does not hold that item in
// the required count. No bot action crafts for a quest objective on the active profile (ProfessionUse real-reagent pick =
// skill-ups only), so holding the count is the only way the bot reaches it; "knows a creating spell" joins with W2.
// Held quests stay untouched (no abandon, no objective skip): their craft-only objective has no travel point anyway (objective
// points come from loot sources only); a counter (held_craft_only=) shows them.
// Pure rules here; the data is built once at start from the world DB (PlayerbotAI::BuildCraftOnlyQuests).

#include <cstddef>
#include <cstdint>
#include <string>

namespace ai::quest_craft_only
{
// Mode 0 off, 1 all, 2 cohort B, 3 cohort A (CohortPolicy.h); > 3 = 0 with an error line (PlayerbotAIConfig.cpp).
constexpr std::uint32_t ModeMax = 3;

// Skill line categories of the professions in SkillLine.dbc (11 = professions, 9 = secondary skills: cooking, first aid, fishing).
constexpr std::uint32_t CategoryProfession = 11;
constexpr std::uint32_t CategorySecondary = 9;

inline bool IsProfessionCategory(std::uint32_t category)
{
    return category == CategoryProfession || category == CategorySecondary;
}

// Every known source of one item. Unknown = a source (the builder errs towards "has another source").
struct Sources
{
    bool professionRecipe = false;   // a create-item spell on a profession skill line makes it
    bool otherSpell = false;         // a create-item spell outside the profession skill lines makes it (quest / event spells)
    bool loot = false;               // any *_loot_template row (creature, GO, item, reference, pickpocket, fishing, skinning, ...)
    bool vendor = false;             // npc_vendor or npc_vendor_template
    bool questGiven = false;         // a quest reward, choice reward or source item
};

inline bool CraftOnly(Sources const& s)
{
    return s.professionRecipe && !s.otherSpell && !s.loot && !s.vendor && !s.questGiven;
}

// One craft-only objective of a quest and what the bot holds of it.
struct Need
{
    std::uint32_t itemId = 0;
    std::uint32_t required = 0;
    std::uint32_t held = 0;
};

// Index of the first craft-only objective the bot does not hold in the required count, -1 when it holds all of them (or there
// are none): only an unmet objective blocks the quest.
inline int FirstUnmet(Need const* needs, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        if (needs[i].held < needs[i].required)
            return int(i);
    return -1;
}

enum class Path : std::uint8_t
{
    Accept,
    Giver,
    Held,
};

inline char const* PathName(Path path)
{
    switch (path)
    {
        case Path::Accept: return "accept";
        case Path::Giver: return "giver";
        case Path::Held: return "held";
    }
    return "unknown";
}

// The skip line keeps the [QuestFeasibility] state=skip shape with its own reason name; a held quest is state=held (nothing is
// skipped or dropped for it).
inline std::string FormatSkipLine(std::uint32_t bot, std::uint32_t level, std::uint32_t questId, Path path, Need const& need)
{
    return std::string("[QuestFeasibility] state=") + (path == Path::Held ? "held" : "skip") + " bot=" + std::to_string(bot) +
        " level=" + std::to_string(level) + " quest=" +
        std::to_string(questId) + " reason=craft_only path=" + PathName(path) + " item=" + std::to_string(need.itemId) + " held=" +
        std::to_string(need.held) + " required=" + std::to_string(need.required);
}

// Once at start (only with the key on): how many items are craft-only and how many quests ask for one.
inline std::string FormatInitLine(std::uint32_t items, std::uint32_t quests, std::uint32_t mode)
{
    return "[QuestCraftOnly] init mode=" + std::to_string(mode) + " craft_only_items=" + std::to_string(items) + " quests=" +
        std::to_string(quests);
}

// The built set by id (OB-10 Q1(b), acceptance WIRING): one line per craft-only objective, sorted by quest, once per build.
inline std::string FormatQuestLine(std::uint32_t quest, std::uint32_t item, std::uint32_t required)
{
    return "[QuestCraftOnly] quest=" + std::to_string(quest) + " item=" + std::to_string(item) + " required=" +
        std::to_string(required);
}

// Once a minute (world thread, only with the key on): accept skips and giver relations found inactive count checks; held counts
// (bot, quest) pairs whose line was due (at most one per bot, quest and hour).
inline std::string FormatMinuteLine(std::uint32_t skips, std::uint32_t giverInactive, std::uint32_t held)
{
    return "[QuestCraftOnly] minute skips=" + std::to_string(skips) + " giver_inactive=" + std::to_string(giverInactive) +
        " held_craft_only=" + std::to_string(held);
}
}
