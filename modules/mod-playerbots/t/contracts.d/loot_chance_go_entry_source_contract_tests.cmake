# OB-00 08.11.2026 (bug fix: code default 1, 0 = rollback): AiPlayerbot.LootChance.GameObjectEntryFix. The drop map keeps a game
# object entry negated; LootChanceValue::Calculate put that negative int32 into ObjectGuid(HIGHGUID_GAMEOBJECT, entry, 1) as it
# was, the uint32 0xFFFF.... << 24 ran into the high guid bits 48..63, the guid was no game object guid and every game object had
# loot chance 0. The fix passes the positive entry (LootChancePolicy.h; t/loot_chance_policy_tests.cpp shows a game object guid
# without high guid overflow over the 24-bit entry range).
# The lever AiPlayerbot.QuestRelation.GameObjectChanceDrops (default 0) keeps the quest relation rule of QuestValues.cpp as it was
# with the bug: a game object becomes a quest objective only through a quest item, so quest travel is unchanged by the fix alone
# (lever 1: 292 quests / 204 spawned chest entries, world DB 08.11.).
# This contract pins both keys (dist, member, read, [ConfigEcho] sorted), both call sites, the policy constants against the core's
# ObjectGuid.h and the policy test registration.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "LootChance GO entry: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "LootChance GO entry: ${description}: ${needle}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/LootChancePolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/values/LootValues.cpp" loot)
read_source("${PB_SOURCE_DIR}/strategy/values/QuestValues.cpp" quest)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)
read_source("${CORE_SOURCE_DIR}/src/game/ObjectGuid.h" guid_h)

# 1. LootChance.GameObjectEntryFix: default 1 in the dist, the member and the read; 0 = rollback, anything else = 1.
require_text("${dist}" "\nAiPlayerbot.LootChance.GameObjectEntryFix = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 lootChanceGameObjectEntryFix = 1;" "config member, default 1")
require_text("${config_cpp}" "lootChanceGameObjectEntryFix = ai::loot_chance::FixOn(config.GetIntDefault(\"AiPlayerbot.LootChance.GameObjectEntryFix\", 1)) ? 1 : 0;" "config read, unknown = 1")
require_text("${policy}" "inline bool FixOn(int value)\n{\n    return value != 0;\n}" "0 = rollback, anything else = 1")
require_text("${config_cpp}" "            {\"Loot.RoleWeights\", Value(lootRoleWeights)},\n            {\"LootChance.GameObjectEntryFix\", Value(lootChanceGameObjectEntryFix)},\n            {\"Path.IslandGuard\"" "[ConfigEcho] entry, sorted (owner rule #615, #710)")

# 2. QuestRelation.GameObjectChanceDrops: a lever, default 0; anything but 1 = 0.
require_text("${dist}" "\nAiPlayerbot.QuestRelation.GameObjectChanceDrops = 0\n" "dist key, default 0")
require_text("${config_h}" "uint32 questRelationGameObjectChanceDrops = 0;" "config member, default 0")
require_text("${config_cpp}" "questRelationGameObjectChanceDrops = ai::loot_chance::ChanceDropsOn(config.GetIntDefault(\"AiPlayerbot.QuestRelation.GameObjectChanceDrops\", 0)) ? 1 : 0;" "config read, unknown = 0")
require_text("${policy}" "inline bool ChanceDropsOn(int value)\n{\n    return value == 1;\n}" "only 1 turns the lever on")
require_text("${config_cpp}" "            {\"QuestLoop.TurnInWorkDeathNotRoute\", Value(questLoopTurnInWorkDeathNotRoute)},\n            {\"QuestRelation.GameObjectChanceDrops\", Value(questRelationGameObjectChanceDrops)},\n            {\"QuestRescue." "[ConfigEcho] entry, sorted (next key QuestRescue.*)")

# 3. LootChanceValue::Calculate: the creature guid unchanged, the game object guid with the positive entry behind the switch.
require_text("${loot}" "#include \"playerbot/LootChancePolicy.h\"" "policy include")
require_text("${loot}" "float LootChanceValue::Calculate()\n{\n\tint32 entry = getMultiQualifierInt(getQualifier(), 0, \" \");" "the signed qualifier entry")
require_text("${loot}" "\tif (entry > 0)\n\t\tlTemplateA = DropMapValue::GetLootTemplate(ObjectGuid(HIGHGUID_UNIT, entry, uint32(1)), LOOT_CORPSE);\n\telse\n\t\tlTemplateA = DropMapValue::GetLootTemplate(ObjectGuid(HIGHGUID_GAMEOBJECT, ai::loot_chance::TemplateEntry(entry, sPlayerbotAIConfig.lootChanceGameObjectEntryFix != 0), uint32(1)), LOOT_CORPSE);" "game object guid through TemplateEntry")
# The body of LootChanceValue::Calculate (anchored on both function heads; the drop map above builds its game object guid from
# the positive sGOStorage id, which is fine).
string(FIND "${loot}" "float LootChanceValue::Calculate()" chance_begin)
string(FIND "${loot}" "itemUsageMap EntryLootUsageValue::Calculate()" chance_end)
if(chance_begin EQUAL -1 OR chance_end LESS_EQUAL chance_begin)
  message(FATAL_ERROR "LootChance GO entry: LootChanceValue::Calculate not found before EntryLootUsageValue::Calculate")
endif()
math(EXPR chance_len "${chance_end} - ${chance_begin}")
string(SUBSTRING "${loot}" ${chance_begin} ${chance_len} chance_body)
forbid_text("${chance_body}" "ObjectGuid(HIGHGUID_GAMEOBJECT, entry," "the overflowing game object guid is back in LootChanceValue::Calculate")
require_text("${policy}" "    if (qualifierEntry < 0 && fix)\n        return std::uint32_t(-std::int64_t(qualifierEntry));\n    return std::uint32_t(qualifierEntry);" "TemplateEntry: positive entry with the fix, raw value without")

# 4. The quest relation rule: game objects only through a quest item unless the lever is on; creatures as before (> 5 percent).
require_text("${quest}" "#include \"playerbot/LootChancePolicy.h\"" "policy include")
require_text("${quest}" "\t\t\t\t\t\tfor (auto& entry : GAI_VALUE2(std::list<int32>, \"item drop list\", itemId))" "signed drop list entry")
require_text("${quest}" "\t\t\t\t\t\t\tbool const chanceDrops = sPlayerbotAIConfig.questRelationGameObjectChanceDrops != 0;\n\t\t\t\t\t\t\tif (proto->Class == ITEM_CLASS_QUEST || (ai::loot_chance::AskChance(entry, chanceDrops) && GAI_VALUE2(float, \"loot chance\", chanceQualifier) > ai::loot_chance::RelationMinChance))\n\t\t\t\t\t\t\t\trMap[entry][questId] |= relationFlag;" "relation rule behind the lever")
require_text("${policy}" "inline bool AskChance(std::int32_t entry, bool chanceDrops)\n{\n    return entry > 0 || chanceDrops;\n}" "creatures always, game objects only with the lever")
require_text("${policy}" "constexpr float RelationMinChance = 5.0f;" "the 5 percent threshold as before")
string(REGEX MATCHALL "\"loot chance\"" chance_reads "${quest}")
list(LENGTH chance_reads chance_read_count)
if(NOT chance_read_count EQUAL 1)
  message(FATAL_ERROR "LootChance GO entry: QuestValues.cpp must read \"loot chance\" exactly once (the relation rule), found ${chance_read_count}")
endif()

# 5. The policy mirrors the core guid: HIGHGUID values, the constructor and the accessors of src/game/ObjectGuid.h.
require_text("${guid_h}" "    HIGHGUID_GAMEOBJECT     = 0xF110," "core HIGHGUID_GAMEOBJECT")
require_text("${guid_h}" "    HIGHGUID_UNIT           = 0xF130," "core HIGHGUID_UNIT")
require_text("${guid_h}" "ObjectGuid(HighGuid hi, uint32 entry, uint32 counter) : m_guid(counter ? uint64(counter) | (uint64(entry) << 24) | (uint64(hi) << 48) : 0) {}" "core guid constructor")
require_text("${guid_h}" "static HighGuid GetHigh(uint64 guid) { return HighGuid((guid >> 48) & 0x0000FFFF); }" "core GetHigh")
require_text("${guid_h}" "uint32   GetEntry() const { return HasEntry() ? uint32((m_guid >> 24) & UI64LIT(0x0000000000FFFFFF)) : 0; }" "core GetEntry")
require_text("${policy}" "constexpr std::uint32_t HighGuidGameObject = 0xF110;" "policy HIGHGUID_GAMEOBJECT")
require_text("${policy}" "constexpr std::uint32_t HighGuidUnit = 0xF130;" "policy HIGHGUID_UNIT")
require_text("${policy}" "return counter ? std::uint64_t(counter) | (std::uint64_t(entry) << 24) | (std::uint64_t(hi) << 48) : 0;" "policy guid packing = core constructor")
require_text("${policy}" "return std::uint32_t((guid >> 48) & 0x0000FFFF);" "policy GetHigh = core")
require_text("${policy}" "return std::uint32_t((guid >> 24) & 0x0000000000FFFFFF);" "policy GetEntry = core")

# 6. The policy test is registered and compiled.
require_text("${tests}" "add_executable(loot_chance_policy_tests\n  \"\${PB_MODULE_DIR}/t/loot_chance_policy_tests.cpp\")" "policy test target")
require_text("${tests}" "add_test(NAME loot_chance_policy\n  COMMAND loot_chance_policy_tests" "policy test registration")
