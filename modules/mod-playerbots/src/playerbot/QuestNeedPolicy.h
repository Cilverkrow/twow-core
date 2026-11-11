#pragma once

#include "CohortPolicy.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

// A3 step 2 H5 (A3-HARM-UPSTREAM H5, OB-00 09.11./10.11.2026; AiPlayerbot.QuestBook.NeedPerObjective + .ItemUse, default 0):
// spec ws-10/cli-quest-organic/a3/step2-questbook/h5/H5-SPEC-PREREG-DRAFT.md.
//   (a) the "needed quest entries" set holds the entries of the OPEN objectives of a quest only (one destination fetch with the
//       mask of the open objective bits), not those of every objective once any one is open;
//   (b) a quest item is used on a target only when an open objective links the item to that target (L1 spell credit, L2 source
//       item), never by dice, with an attempt cap per target and per item.
// Level gate (OB-00 10.11. option A): both parts act only at bot level >= MinLevel (below: the old behaviour, fail-open).
// Pure rules: no game types, no config reads, no locks, no log calls. Times: uint32 ms of WorldTimer::getMSTime() (wrap-safe)
// for the cap, uint32 unix seconds for the hour lines.
namespace ai::quest_need
{
// ---- the switch -----------------------------------------------------------------------------------------------------------
// AiPlayerbot.QuestBook.NeedPerObjective: 0 off (default, rollback), 1 all, 2 cohort B (odd low guid), 3 cohort A (even low guid);
// any other value is 0 (the loader writes one error line). ItemUse acts only where NeedPerObjective applies (one lever).
constexpr std::uint32_t MinLevel = 10;

inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}

inline std::uint32_t SanitizeMode(std::uint32_t mode)
{
    return ValidMode(mode) ? mode : cohort::ModeOff;
}

// H5Applies: the switch applies to this bot (cohort) AND the bot is at level >= MinLevel. Mode 0 / unknown: never.
inline bool H5Applies(std::uint32_t mode, std::uint32_t guidLow, std::uint32_t level)
{
    return level >= MinLevel && cohort::Applies(SanitizeMode(mode), guidLow);
}

// ---- (a) the open-objective mask --------------------------------------------------------------------------------------------
// The travel destinations of quest objective j are registered under the purpose bit 1 << (j + 1) (TravelValues.h
// QuestObjective1..4); a mask of these bits fetches exactly the destinations of those objectives.
inline std::uint32_t ObjectiveBit(std::uint32_t objective)
{
    return objective < 4 ? (1u << (objective + 1)) : 0u;
}

// A grind pick is "stale" when its entry serves no open objective of an incomplete quest but serves a complete one.
inline bool StalePick(bool servesOpen, bool servesDone)
{
    return !servesOpen && servesDone;
}

// ---- (b) the link rule ------------------------------------------------------------------------------------------------------
// One open objective j of an incomplete quest, seen from the item and the target.
struct ObjectiveLink
{
    bool targetMatches = false;   // the target's entry is ReqCreatureOrGOId[j] (> 0 creature, < 0 game object)
    bool open = false;            // count < ReqCreatureOrGOCount[j]
    bool killOrCast = false;      // the quest has QUEST_SPECIAL_FLAG_KILL_OR_CAST
    bool spellMatches = false;    // ReqSpell[j] == the item's spell
    bool sourceItem = false;      // the item is the quest's SrcItemId or one of its ReqSourceId[]
};

enum class LinkKind : std::uint8_t
{
    None = 0,
    SpellCredit = 1,   // L1
    SourceItem = 2,    // L2
};

inline LinkKind Link(ObjectiveLink const& o)
{
    if (!o.targetMatches || !o.open)
        return LinkKind::None;
    if (o.killOrCast && o.spellMatches)
        return LinkKind::SpellCredit;
    if (o.sourceItem)
        return LinkKind::SourceItem;
    return LinkKind::None;
}

// The credit key of a target: the entry as SMSG_QUESTUPDATE_ADD_KILL carries it (game objects with the high bit).
inline std::uint32_t CreditKey(std::uint32_t entry, bool gameObject)
{
    return gameObject ? (entry | 0x80000000u) : entry;
}

// ---- (b) the attempt cap ----------------------------------------------------------------------------------------------------
struct CapParams
{
    std::uint32_t creditWindowMs = 15000;   // a use without a credit for the target's entry within this is a miss
    std::uint32_t targetMisses = 2;         // misses on one target guid ...
    std::uint32_t targetRefuseMs = 600000;  // ... refuse that guid this long
    std::uint32_t itemMisses = 6;           // misses of one item within itemWindowMs ...
    std::uint32_t itemWindowMs = 600000;
    std::uint32_t itemRefuseMs = 600000;    // ... refuse the item this long
    std::size_t maxPending = 16;            // open uses kept; the oldest beyond this is judged at once (a miss)
    std::size_t maxTracked = 256;           // targets / items tracked; beyond this the oldest finished entries are dropped
};

inline bool Before(std::uint32_t a, std::uint32_t b)
{
    return std::int32_t(a - b) < 0;
}

enum class CapVerdict : std::uint8_t
{
    Allowed = 0,
    RefusedTarget = 1,
    RefusedItem = 2,
};

class ItemUseCap
{
public:
    explicit ItemUseCap(CapParams params = CapParams()) : params_(params) {}

    // Is a use of `item` on `target` allowed now? Judges the expired uses first.
    CapVerdict Check(std::uint32_t item, std::uint64_t target, std::uint32_t nowMs)
    {
        Expire(nowMs);
        auto const it = items_.find(item);
        if (it != items_.end() && it->second.refusedUntil && Before(nowMs, it->second.refusedUntil))
            return CapVerdict::RefusedItem;
        auto const tt = targets_.find(target);
        if (tt != targets_.end() && tt->second.refusedUntil && Before(nowMs, tt->second.refusedUntil))
            return CapVerdict::RefusedTarget;
        return CapVerdict::Allowed;
    }

    // A use happened: it waits creditWindowMs for a credit of `creditKey`.
    void NoteUse(std::uint32_t item, std::uint64_t target, std::uint32_t creditKey, std::uint32_t nowMs)
    {
        Expire(nowMs);
        if (pending_.size() >= params_.maxPending)
        {
            Judge(pending_.front(), nowMs);
            pending_.erase(pending_.begin());
        }
        pending_.push_back(PendingUse{item, target, creditKey, nowMs});
    }

    // A kill / cast credit for `creditKey` arrived: every open use with that key within the window is credited (no miss).
    // Returns the number of uses credited.
    std::uint32_t NoteCredit(std::uint32_t creditKey, std::uint32_t nowMs)
    {
        Expire(nowMs);
        std::uint32_t n = 0;
        for (std::size_t i = 0; i < pending_.size();)
        {
            if (pending_[i].creditKey == creditKey)
            {
                ++n;
                pending_.erase(pending_.begin() + std::ptrdiff_t(i));
            }
            else
                ++i;
        }
        return n;
    }

    // Judges every open use older than the credit window (a miss each).
    void Expire(std::uint32_t nowMs)
    {
        while (!pending_.empty() && !Before(nowMs, pending_.front().at + params_.creditWindowMs))
        {
            Judge(pending_.front(), nowMs);
            pending_.erase(pending_.begin());
        }
    }

    std::size_t PendingCount() const { return pending_.size(); }
    std::size_t TrackedTargets() const { return targets_.size(); }
    std::size_t TrackedItems() const { return items_.size(); }

private:
    struct PendingUse
    {
        std::uint32_t item;
        std::uint64_t target;
        std::uint32_t creditKey;
        std::uint32_t at;
    };

    struct TargetState
    {
        std::uint32_t misses = 0;
        std::uint32_t refusedUntil = 0;   // 0 = not refused
        std::uint32_t last = 0;
    };

    struct ItemState
    {
        std::vector<std::uint32_t> missTimes;
        std::uint32_t refusedUntil = 0;
        std::uint32_t last = 0;
    };

    void Judge(PendingUse const& use, std::uint32_t nowMs)
    {
        TargetState& t = targets_[use.target];
        if (t.refusedUntil && !Before(nowMs, t.refusedUntil))
        {
            t.refusedUntil = 0;
            t.misses = 0;
        }
        t.last = nowMs;
        if (++t.misses >= params_.targetMisses)
        {
            t.refusedUntil = Nz(nowMs + params_.targetRefuseMs);
            t.misses = 0;
        }

        ItemState& s = items_[use.item];
        if (s.refusedUntil && !Before(nowMs, s.refusedUntil))
            s.refusedUntil = 0;
        s.last = nowMs;
        std::vector<std::uint32_t> kept;
        for (std::uint32_t at : s.missTimes)
            if (Before(nowMs, at + params_.itemWindowMs))
                kept.push_back(at);
        kept.push_back(nowMs);
        s.missTimes.swap(kept);
        if (s.missTimes.size() >= params_.itemMisses)
        {
            s.refusedUntil = Nz(nowMs + params_.itemRefuseMs);
            s.missTimes.clear();
        }
        Prune(nowMs);
    }

    // 0 means "not refused": a refusal that would end at tick 0 ends one ms later.
    static std::uint32_t Nz(std::uint32_t t) { return t ? t : 1u; }

    void Prune(std::uint32_t nowMs)
    {
        std::uint32_t const keep = params_.targetRefuseMs > params_.itemWindowMs ? params_.targetRefuseMs : params_.itemWindowMs;
        if (targets_.size() > params_.maxTracked)
            PruneMap(targets_, nowMs, keep);
        if (items_.size() > params_.maxTracked)
            PruneMap(items_, nowMs, keep);
    }

    // drops the entries that are not refused and saw no miss for `keep` ms
    template <class Map>
    static void PruneMap(Map& map, std::uint32_t nowMs, std::uint32_t keep)
    {
        for (auto it = map.begin(); it != map.end();)
        {
            bool const refused = it->second.refusedUntil && Before(nowMs, it->second.refusedUntil);
            if (!refused && !Before(nowMs, it->second.last + keep))
                it = map.erase(it);
            else
                ++it;
        }
    }

    CapParams params_;
    std::vector<PendingUse> pending_;
    std::unordered_map<std::uint64_t, TargetState> targets_;
    std::unordered_map<std::uint32_t, ItemState> items_;
};

// ---- the hour lines (AiPlayerbot.QuestBook.NeedPerObjective.Log, both cohorts, measurement only) -----------------------------
struct HourStats
{
    std::uint32_t picksQuest = 0;      // grind picks whose source is the quest need (live set)
    std::uint32_t picksStale = 0;      // ... whose entry serves only complete objectives (direct check, both arms)
    std::uint32_t calc = 0;            // live NeededQuestEntriesValue::Calculate runs
    std::uint64_t calcUs = 0;          // their summed steady-clock microseconds
    std::uint64_t setN = 0;            // their summed set sizes
    std::uint32_t uses = 0;            // quest item uses on an rpg target
    std::uint32_t credited = 0;        // uses with a kill / cast credit of the target's entry within the credit window
    std::uint32_t linked = 0;          // (b) evaluations with a link (L1 / L2)
    std::uint32_t refusedCap = 0;      // (b) linked but refused by the cap
    std::uint32_t refusedNolink = 0;   // (b) castable, but no open objective links item and target
};

// The clock hour of the counters. Due once a later clock hour has begun; the first call only starts the hour.
struct HourClock
{
    std::uint32_t hourStart = 0;

    static std::uint32_t StartOf(std::uint32_t nowSec) { return nowSec - nowSec % 3600u; }

    bool Due(std::uint32_t nowSec)
    {
        if (!hourStart)
        {
            hourStart = StartOf(nowSec);
            return false;
        }
        return StartOf(nowSec) != hourStart;
    }
};

// One per-bot state: the cap (ItemUse), the counters and their hour (Log). Owned by PlayerbotAI, memory only, gone at logout.
struct BotState
{
    ItemUseCap cap;
    HourStats stats;
    HourClock clock;
    // as of the last hour tick: the line of a bot that logs out reads no live state
    std::uint32_t guidLow = 0;
    std::uint32_t level = 0;
    bool applies = false;          // part (a)
    bool itemUseApplies = false;   // part (b)
};

inline std::string FormatNeedLine(std::uint32_t guidLow, std::uint32_t level, bool applies, std::uint32_t hourStart,
    HourStats const& s)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "[QuestNeed] bot=%u cohort=%c level=%u applies=%u hour=%u picks_quest=%u picks_stale=%u "
        "calc=%u calc_us=%llu set_n=%llu", guidLow, cohort::Letter(guidLow), level, applies ? 1u : 0u, hourStart, s.picksQuest,
        s.picksStale, s.calc, static_cast<unsigned long long>(s.calcUs), static_cast<unsigned long long>(s.setN));
    return buf;
}

inline std::string FormatItemUseLine(std::uint32_t guidLow, std::uint32_t level, bool applies, std::uint32_t hourStart,
    HourStats const& s)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "[QuestItemUse] bot=%u cohort=%c level=%u applies=%u hour=%u uses=%u credited=%u linked=%u "
        "refused_cap=%u refused_nolink=%u", guidLow, cohort::Letter(guidLow), level, applies ? 1u : 0u, hourStart, s.uses,
        s.credited, s.linked, s.refusedCap, s.refusedNolink);
    return buf;
}
}
