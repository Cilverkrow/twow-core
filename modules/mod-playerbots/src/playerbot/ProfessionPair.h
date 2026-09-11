#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace ai::profession
{
constexpr std::uint32_t kPlanVersion = 1;
// Version 1 remains the per-bot legacy plan selected on first login. Version
// 2 is deliberately a separate *policy* version for the explicit, ordered
// persistent-roster materializer below. Nothing in the normal login path may
// invoke it: a roster version is an administrative decision, not a side effect
// of login, restart, or scale-up.
constexpr std::uint32_t kExactRosterPlanVersion = 2;
constexpr std::size_t kExactRosterBaseSize = 68;
constexpr char const* kEventName = "profession_pair";
constexpr char const* kEventData = "v1";

enum Pair : std::uint32_t
{
    None,
    HerbalismAlchemy,
    SkinningLeatherworking,
    MiningBlacksmithing,
    MiningEngineering,
    MiningJewelcrafting,
    TailoringEnchanting
};

struct Definition
{
    Pair pair;
    std::uint32_t first;
    std::uint32_t second;
};

// `members` is the PersistentActiveRoster snapshot order. It is intentionally
// not sorted here: the caller must supply the persisted GUID order, and an
// EXPAND operation preserves its old vector as an immutable prefix.
struct RosterMember
{
    std::uint32_t guid;
    std::uint8_t classId;
};

struct PlanAssignment
{
    std::uint32_t guid;
    Pair pair;

    bool operator==(PlanAssignment const& other) const
    {
        return guid == other.guid && pair == other.pair;
    }
};

struct ExactQuota
{
    Pair pair;
    std::uint32_t count;
};

struct ExactRosterPlan
{
    std::uint32_t version = kExactRosterPlanVersion;
    std::vector<PlanAssignment> assignments;
};

enum class ExactPlanResult
{
    Success,
    InvalidRoster,
    ExistingPlanRequired,
    InvalidExistingPlan,
    ExistingPlanConflict
};

constexpr std::uint32_t kAlchemy = 171;
constexpr std::uint32_t kBlacksmithing = 164;
constexpr std::uint32_t kEnchanting = 333;
constexpr std::uint32_t kEngineering = 202;
constexpr std::uint32_t kHerbalism = 182;
constexpr std::uint32_t kJewelcrafting = 755;
constexpr std::uint32_t kLeatherworking = 165;
constexpr std::uint32_t kMining = 186;
constexpr std::uint32_t kSkinning = 393;
constexpr std::uint32_t kTailoring = 197;

inline Definition const* Find(Pair pair)
{
    static Definition const definitions[] =
    {
        { HerbalismAlchemy, kHerbalism, kAlchemy },
        { SkinningLeatherworking, kSkinning, kLeatherworking },
        { MiningBlacksmithing, kMining, kBlacksmithing },
        { MiningEngineering, kMining, kEngineering },
        { MiningJewelcrafting, kMining, kJewelcrafting },
        { TailoringEnchanting, kTailoring, kEnchanting }
    };

    for (Definition const& definition : definitions)
    {
        if (definition.pair == pair)
            return &definition;
    }

    return nullptr;
}

inline bool IsValid(std::uint32_t value)
{
    return Find(static_cast<Pair>(value)) != nullptr;
}

inline bool Contains(Pair pair, std::uint32_t skill)
{
    if (Definition const* definition = Find(pair))
        return definition->first == skill || definition->second == skill;

    return false;
}

inline std::uint32_t Mix(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

inline std::uint32_t Weight(std::uint8_t classId, Pair pair)
{
    std::uint32_t herbalism = 2;
    std::uint32_t skinning = 1;
    std::uint32_t mining = 1;
    std::uint32_t tailoring = 3;

    switch (classId)
    {
        case 1: // warrior
        case 6: // death knight
            skinning = 2;
            mining = 3;
            tailoring = 1;
            break;
        case 2: // paladin
            mining = 3;
            tailoring = 2;
            break;
        case 3: // hunter
            skinning = 3;
            mining = 3;
            tailoring = 1;
            break;
        case 4: // rogue
            herbalism = 1;
            skinning = 3;
            mining = 2;
            tailoring = 1;
            break;
        case 5: // priest
        case 8: // mage
        case 9: // warlock
            break;
        case 7: // shaman
            herbalism = 3;
            skinning = 3;
            mining = 2;
            tailoring = 1;
            break;
        case 11: // druid
            herbalism = 3;
            skinning = 3;
            tailoring = 1;
            break;
        default:
            break;
    }

    switch (pair)
    {
        case HerbalismAlchemy:
            return herbalism * 7;
        case SkinningLeatherworking:
            return skinning * 7;
        case MiningBlacksmithing:
            return mining * 3;
        case MiningEngineering:
        case MiningJewelcrafting:
            return mining * 2;
        case TailoringEnchanting:
            return tailoring * 7;
        case None:
            return 0;
    }

    return 0;
}

inline Pair Select(std::uint32_t guid, std::uint8_t classId, std::uint32_t requiredSkill = 0)
{
    Pair const pairs[] =
    {
        HerbalismAlchemy,
        SkinningLeatherworking,
        MiningBlacksmithing,
        MiningEngineering,
        MiningJewelcrafting,
        TailoringEnchanting
    };

    std::uint32_t total = 0;
    for (Pair pair : pairs)
    {
        if (!requiredSkill || Contains(pair, requiredSkill))
            total += Weight(classId, pair);
    }

    if (!total)
        return None;

    std::uint32_t ticket = Mix(guid ^ (kPlanVersion * 0x9e3779b9u)) % total;
    for (Pair pair : pairs)
    {
        if (requiredSkill && !Contains(pair, requiredSkill))
            continue;

        std::uint32_t const weight = Weight(classId, pair);
        if (ticket < weight)
            return pair;

        ticket -= weight;
    }

    return None;
}

// Resolve the non-destructive "grandfathering" case before choosing a new
// plan. Two existing professions must name one unambiguous allowed pair. A
// single existing profession is retained by selecting only from pairs that
// contain it. More than two, duplicate, or incompatible professions yield no
// plan: the caller must retain the character state without changing it.
inline Pair SelectExisting(std::uint32_t guid, std::uint8_t classId,
    std::uint32_t const* skills, std::size_t count)
{
    if (count == 0)
        return Select(guid, classId);

    if (!skills || count > 2)
        return None;

    if (count == 1)
        return Select(guid, classId, skills[0]);

    if (skills[0] == skills[1])
        return None;

    Pair match = None;
    for (Pair candidate : { HerbalismAlchemy, SkinningLeatherworking,
            MiningBlacksmithing, MiningEngineering, MiningJewelcrafting,
            TailoringEnchanting })
    {
        if (!Contains(candidate, skills[0]) || !Contains(candidate, skills[1]))
            continue;

        if (match != None)
            return None;

        match = candidate;
    }

    return match;
}

inline std::array<ExactQuota, 6> ExactRosterQuotas(std::size_t memberCount)
{
    std::array<ExactQuota, 6> quotas =
    {{
        { HerbalismAlchemy, 14 },
        { SkinningLeatherworking, 13 },
        { MiningBlacksmithing, 12 },
        { MiningEngineering, 8 },
        { MiningJewelcrafting, 8 },
        { TailoringEnchanting, 13 }
    }};

    if (memberCount == kExactRosterBaseSize)
        return quotas;

    // Largest-remainder scaling keeps every target deterministic. At 136 it
    // is exactly 2x the approved 68-member distribution; at 500 the pair
    // declaration order breaks equal remainders, never GUID iteration order.
    std::array<std::uint32_t, 6> remainder{};
    std::size_t assigned = 0;
    for (std::size_t index = 0; index < quotas.size(); ++index)
    {
        std::uint64_t const scaled = static_cast<std::uint64_t>(quotas[index].count) * memberCount;
        quotas[index].count = static_cast<std::uint32_t>(scaled / kExactRosterBaseSize);
        remainder[index] = static_cast<std::uint32_t>(scaled % kExactRosterBaseSize);
        assigned += quotas[index].count;
    }

    while (assigned < memberCount)
    {
        std::size_t selected = 0;
        for (std::size_t index = 1; index < remainder.size(); ++index)
            if (remainder[index] > remainder[selected])
                selected = index;
        ++quotas[selected].count;
        remainder[selected] = 0;
        ++assigned;
    }
    return quotas;
}

inline ExactQuota* FindQuota(std::array<ExactQuota, 6>& quotas, Pair pair)
{
    for (ExactQuota& quota : quotas)
        if (quota.pair == pair)
            return &quota;
    return nullptr;
}

inline bool IsStableRoster(std::vector<RosterMember> const& members)
{
    if (members.size() < kExactRosterBaseSize)
        return false;
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        if (!members[index].guid)
            return false;
        for (std::size_t previous = 0; previous < index; ++previous)
            if (members[previous].guid == members[index].guid)
                return false;
    }
    return true;
}

// Pure policy only. The caller supplies GUIDs exclusively from the active
// PersistentActiveRoster snapshot and persists successful assignments through
// the existing profession_pair store. A conflict never mutates learned skills
// or an existing plan; it is reported for explicit administrative resolution.
inline ExactPlanResult MaterializeExactRosterPlan(std::vector<RosterMember> const& members,
    std::vector<PlanAssignment> const& existing, ExactRosterPlan& output, bool authorizedTestReset = false)
{
    output = ExactRosterPlan{};
    if (!IsStableRoster(members) || existing.size() > members.size())
        return ExactPlanResult::InvalidRoster;
    if (members.size() > kExactRosterBaseSize && existing.empty() && !authorizedTestReset)
        return ExactPlanResult::ExistingPlanRequired;

    std::array<ExactQuota, 6> quotas = ExactRosterQuotas(members.size());
    output.assignments.reserve(members.size());
    for (std::size_t index = 0; index < existing.size(); ++index)
    {
        PlanAssignment const& assignment = existing[index];
        if (assignment.guid != members[index].guid || !IsValid(assignment.pair))
            return ExactPlanResult::InvalidExistingPlan;
        ExactQuota* quota = FindQuota(quotas, assignment.pair);
        if (!quota || quota->count == 0)
            return ExactPlanResult::ExistingPlanConflict;
        --quota->count;
        output.assignments.push_back(assignment);
    }

    for (std::size_t index = existing.size(); index < members.size(); ++index)
    {
        RosterMember const& member = members[index];
        ExactQuota const* selected = nullptr;
        std::uint32_t selectedTieBreak = 0;
        for (ExactQuota const& quota : quotas)
        {
            if (!quota.count)
                continue;
            std::uint32_t const tieBreak = Mix(member.guid ^ (static_cast<std::uint32_t>(quota.pair) * 0x9e3779b9u));
            if (!selected || Weight(member.classId, quota.pair) > Weight(member.classId, selected->pair) ||
                (Weight(member.classId, quota.pair) == Weight(member.classId, selected->pair) && tieBreak < selectedTieBreak))
            {
                selected = &quota;
                selectedTieBreak = tieBreak;
            }
        }
        if (!selected)
            return ExactPlanResult::InvalidRoster;
        ExactQuota* remaining = FindQuota(quotas, selected->pair);
        --remaining->count;
        output.assignments.push_back({ member.guid, selected->pair });
    }

    for (ExactQuota const& quota : quotas)
        if (quota.count != 0)
            return ExactPlanResult::InvalidRoster;
    return ExactPlanResult::Success;
}
}
