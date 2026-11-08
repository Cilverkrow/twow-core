#pragma once

#include <cstdint>

namespace ai::loot_chance
{
// AiPlayerbot.LootChance.GameObjectEntryFix (OB-00 08.11.2026, bug fix: code default 1, 0 = rollback, any other value = 1).
// Pure rules, no game types, no config reads, no logging.
//
// The "loot chance" qualifier carries a creature entry as it is and a game object entry negated, as the drop map stores it
// (LootValues.cpp, DropMapValue). LootChanceValue::Calculate put the negative int32 into ObjectGuid(HIGHGUID_GAMEOBJECT,
// entry, 1) as it was: the uint32 entry 0xFFFF.... shifted by 24 runs into the high guid bits 48..63, so the guid is no game
// object guid, DropMapValue::GetLootTemplate finds no template and every game object got loot chance 0.

// ObjectGuid.h of the core: HIGHGUID_GAMEOBJECT and HIGHGUID_UNIT (the contract pins both values).
constexpr std::uint32_t HighGuidGameObject = 0xF110;
constexpr std::uint32_t HighGuidUnit = 0xF130;

// Mode: 0 = as before (rollback), 1 = fixed (default); any other value acts like 1.
inline bool FixOn(int value)
{
    return value != 0;
}

// The entry for the loot template lookup: a creature entry as it is; a game object entry (negative in the qualifier) as its
// positive entry with the fix, the old raw value without it.
inline std::uint32_t TemplateEntry(std::int32_t qualifierEntry, bool fix)
{
    if (qualifierEntry < 0 && fix)
        return std::uint32_t(-std::int64_t(qualifierEntry));
    return std::uint32_t(qualifierEntry);
}

// AiPlayerbot.QuestRelation.GameObjectChanceDrops (OB-00 08.11.2026, a lever: default 0, any other value than 1 = 0).
// The quest objective rule of "entry quest relation" (QuestValues.cpp): an entry that drops a quest objective item becomes an
// objective of that quest when the item is a quest item, or when its loot chance is above 5 percent. With the bug every game
// object had chance 0, so a game object came in only through a quest item. With the fix the chance is right; the lever keeps
// that rule (0) or lets game objects in by chance as creatures (1). World DB 08.11.: 1 adds 292 quests with new objective
// destinations at 204 spawned chest / crate entries - a quest travel change, measured run to run before it is turned on.
inline bool ChanceDropsOn(int value)
{
    return value == 1;
}

constexpr float RelationMinChance = 5.0f;

// Whether the chance is asked at all: creatures always, game objects only with the lever.
inline bool AskChance(std::int32_t entry, bool chanceDrops)
{
    return entry > 0 || chanceDrops;
}

// The whole rule; chance is only read when AskChance is true (the caller computes it lazily).
inline bool IsObjectiveDrop(std::int32_t entry, bool questItem, bool chanceDrops, float chance)
{
    return questItem || (AskChance(entry, chanceDrops) && chance > RelationMinChance);
}

// The core's ObjectGuid(HighGuid hi, uint32 entry, uint32 counter) and its GetHigh / GetEntry (src/game/ObjectGuid.h; the
// contract pins the constructor and the accessors), so the unit test can show the guid the lookup gets.
inline std::uint64_t PackGuid(std::uint32_t hi, std::uint32_t entry, std::uint32_t counter)
{
    return counter ? std::uint64_t(counter) | (std::uint64_t(entry) << 24) | (std::uint64_t(hi) << 48) : 0;
}

inline std::uint32_t HighOf(std::uint64_t guid)
{
    return std::uint32_t((guid >> 48) & 0x0000FFFF);
}

inline std::uint32_t EntryOf(std::uint64_t guid)
{
    return std::uint32_t((guid >> 24) & 0x0000000000FFFFFF);
}
}
