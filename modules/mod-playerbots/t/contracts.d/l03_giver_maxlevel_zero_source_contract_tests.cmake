cmake_policy(VERSION 3.16)
# twow-repo#738 L03 (AiPlayerbot.QuestGiver.MaxLevelZeroFix, OB-10 decision 085028ec 21.11.2026; default 1 since the
# K-2 ADOPT, OB-10 001f4ea2 + owner 22.11.2026, 0 = rollback): the quest giver trip check (QuestRelationTravelDestination::IsPossible, giver relation) compared the
# bot level against an unsigned quest MaxLevel without "0 = no upper limit", so every quest with MaxLevel 0 (about 97 % of
# the templates) was refused (giver_drops=...min_max). Pins: the core semantics (Player::CanTakeQuest: MaxLevel 0 = open; the
# getter is uint32), the bot's quest search check (ChooseTravelTargetAction.cpp) with the same semantics, the keyed fix with
# the old compare kept for key 0, the GiverDropMinMaxLevel counter, the cohort bit (PlayerTravelInfo, bot thread) and the key
# wiring (member, read, clamp, echo, dist).

foreach(var PB_SOURCE_DIR CORE_SOURCE_DIR)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "L03 giver MaxLevel 0: ${var} is required")
  endif()
endforeach()

function(read_source path out)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "L03 giver MaxLevel 0: missing file ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(need text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "L03 giver MaxLevel 0: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "L03 giver MaxLevel 0: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(need_before text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "L03 giver MaxLevel 0: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(need_count text needle expected description)
  string(REPLACE "${needle}" "" stripped "${text}")
  string(LENGTH "${text}" full)
  string(LENGTH "${stripped}" rest)
  string(LENGTH "${needle}" len)
  math(EXPR n "(${full} - ${rest}) / ${len}")
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "L03 giver MaxLevel 0: ${description}: ${n} times, expected ${expected}: ${needle}")
  endif()
endfunction()

read_source("${CORE_SOURCE_DIR}/src/game/Objects/Player.cpp" player_cpp)
read_source("${CORE_SOURCE_DIR}/src/game/QuestDef.h" quest_h)
read_source("${PB_SOURCE_DIR}/TravelMgr.cpp" travel_cpp)
read_source("${PB_SOURCE_DIR}/TravelMgr.h" travel_h)
read_source("${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)

# 1. The core semantics the fix follows: MaxLevel 0 = no upper limit; the getter is unsigned (the reason the old compare fails).
need("${player_cpp}" "    if (pQuest->GetMaxLevel() && pQuest->GetMaxLevel() < GetLevel())" "core CanTakeQuest: MaxLevel 0 = open")
need("${quest_h}" "        uint32 GetMaxLevel() const { return MaxLevel; }" "core uint32 MaxLevel getter")

# 2. The bot's quest search check already reads 0 as open; the keyed giver check must agree with it.
need("${choose_cpp}" "    checks.maxLevel = !quest->GetMaxLevel() || quest->GetMaxLevel() >= bot->GetLevel();" "quest search MaxLevel check (0 = open)")

# 3. The giver check: the bit, the fix (0 = open only for a bot with the bit), the old compare kept otherwise, the counter kept.
set(fix "        bool const maxLevelZeroIsOpen = info.UsesGiverMaxLevelZeroOpen();\n        if ((int32)info.GetLevel() < quest->GetMinLevel() ||\n            ((!maxLevelZeroIsOpen || quest->GetMaxLevel()) && (int32)info.GetLevel() > quest->GetMaxLevel()))\n            return refuse(ai::quest_search::GiverDropMinMaxLevel);\n")
need("${travel_cpp}" "${fix}" "the keyed giver MaxLevel check with its GiverDropMinMaxLevel refusal")
forbid("${travel_cpp}" "        if ((int32)info.GetLevel() < quest->GetMinLevel() || (int32)info.GetLevel() > quest->GetMaxLevel())\n" "the unkeyed old one-line check")
need_count("${travel_cpp}" "refuse(ai::quest_search::GiverDropMinMaxLevel)" 1 "GiverDropMinMaxLevel refusals")
need_count("${travel_cpp}" "UsesGiverMaxLevelZeroOpen()" 1 "bit reads in TravelMgr.cpp")
need_before("${travel_cpp}" "bool QuestRelationTravelDestination::IsPossible(" "bool const maxLevelZeroIsOpen" "fix inside the giver IsPossible")
need_before("${travel_cpp}" "return refuse(ai::quest_search::GiverDropQuestLevel);\n\n        // twow-repo#738 L03" "bool const maxLevelZeroIsOpen" "fix right after the quest level rule")
need_before("${travel_cpp}" "bool const maxLevelZeroIsOpen" "ai::quest_band::GreyGiverSkip(" "fix before the band giver filter")

# 4. The cohort bit: set once in the PlayerTravelInfo constructor (bot thread) from the key; default false.
need("${travel_cpp}" "    giverMaxLevelZeroOpen = ai::cohort::Applies(sPlayerbotAIConfig.questGiverMaxLevelZeroFix, player->GetGUIDLow());\n" "the cohort bit from the key")
need_count("${travel_cpp}" "questGiverMaxLevelZeroFix" 1 "key reads in TravelMgr.cpp (the bit only)")
need_before("${travel_cpp}" "PlayerTravelInfo::PlayerTravelInfo(Player* player, bool gapGrind)" "    giverMaxLevelZeroOpen = ai::cohort::Applies(" "the bit in the PlayerTravelInfo constructor")
need("${travel_h}" "\t\tbool UsesGiverMaxLevelZeroOpen() const { return giverMaxLevelZeroOpen; }" "the bit accessor")
need("${travel_h}" "\t\tbool giverMaxLevelZeroOpen = false;" "the bit default false (default constructor, foreign thread)")

# 5. Key wiring: member default 1, read default 1 with its clamp (an unknown mode > 3 is the default 1), the echo between its
# sorted neighbours, one dist line = 1 with the "Any other value acts like 1." sentence.
need("${config_h}" "    uint32 questGiverMaxLevelZeroFix = 1;" "member default 1")
need("${config_cpp}" "    questGiverMaxLevelZeroFix = config.GetIntDefault(\"AiPlayerbot.QuestGiver.MaxLevelZeroFix\", 1);\n    if (questGiverMaxLevelZeroFix > 3)\n    {\n        sLog.outError(\"[QuestFirstRoute] config key=QuestGiver.MaxLevelZeroFix value=%u result=default 1 (0 off = rollback, 1 all, 2 cohort B, 3 cohort A)\", questGiverMaxLevelZeroFix);\n        questGiverMaxLevelZeroFix = 1;\n    }" "read with default 1 and clamp to 1")
forbid("${config_cpp}" "GetIntDefault(\"AiPlayerbot.QuestGiver.MaxLevelZeroFix\", 0)" "no read default 0")
need("${config_cpp}" "            {\"QuestFirstProgression.TurnInParkFailures\", Value(questFirstProgressionTurnInParkFailures)},\n            {\"QuestGiver.MaxLevelZeroFix\", Value(questGiverMaxLevelZeroFix)},\n            {\"QuestGroups.Enable\", Value(questGroupsEnable)}," "[ConfigEcho] entry in sort order")
need("${dist}" "\nAiPlayerbot.QuestGiver.MaxLevelZeroFix = 1\n" "dist default 1")
need("${dist}" "#   3 = only cohort A (even low guid). Any other value acts like 1.\nAiPlayerbot.QuestGiver.MaxLevelZeroFix = 1\n" "dist: unknown acts like 1")
need_count("${dist}" "AiPlayerbot.QuestGiver.MaxLevelZeroFix =" 1 "dist lines")
