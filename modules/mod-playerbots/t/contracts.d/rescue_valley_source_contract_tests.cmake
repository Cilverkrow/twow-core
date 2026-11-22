cmake_policy(VERSION 3.16)
# twow-repo#575 R2 (AiPlayerbot.QuestRescue.StartValleyFix, OB-10 build OK 22.11.2026 via OB-00, scope answers 1-4; finding
# dauerb-silverpine-20261119/idle-20261122/TRACE-565-2293.txt 4d1371a0; default 0 until its acceptance unit): a roster rescue up to L10
# that falls through to the legacy start-valley branch of RandomPlayerbotMgr::QuestRescueTargets got the OTHER races' start points with
# no distance filter and no dedupe, and the goblin Durotar pushes had no distance check. Pins: the key wiring, the roster + cohort gate,
# the start-valley branch with the fix (own race only, dedupe, rescue distance, last target) and its return before the legacy loop, the
# goblin pushes with the distance, the last-target record after a started teleport only, its clear on progress in the bot's own update,
# and the legacy code for key 0 (start-valley branch, goblin pushes and the random pick unchanged).

foreach(var PB_SOURCE_DIR)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "R2 rescue valley: ${var} is required")
  endif()
endforeach()

function(read_source path out)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "R2 rescue valley: missing file ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(need text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "R2 rescue valley: missing ${description}: ${needle}")
  endif()
endfunction()

function(need_before text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "R2 rescue valley: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(need_count text needle expected description)
  string(REPLACE "${needle}" "" stripped "${text}")
  string(LENGTH "${text}" full)
  string(LENGTH "${stripped}" rest)
  string(LENGTH "${needle}" len)
  math(EXPR n "(${full} - ${rest}) / ${len}")
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "R2 rescue valley: ${description}: ${n} times, expected ${expected}: ${needle}")
  endif()
endfunction()

function(region text start_needle end_needle out)
  string(FIND "${text}" "${start_needle}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "R2 rescue valley: missing region start ${start_needle}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${end_needle}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "R2 rescue valley: missing region end ${end_needle}")
  endif()
  string(SUBSTRING "${tail}" 0 ${b} part)
  set(${out} "${part}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" rpm)
read_source("${PB_SOURCE_DIR}/RescueValleyPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)

region("${rpm}" "std::vector<WorldLocation> RandomPlayerbotMgr::QuestRescueTargets(Player* bot)" "\n}\n" targets)
region("${rpm}" "void RandomPlayerbotMgr::ProcessQuestRescues()" "\n}\n" process)

# 1. The gate: roster bots (startZoneRules) and the cohort key, once, before the HomeHubs block.
need("${targets}" "    bool const valleyFix = startZoneRules && ai::cohort::Applies(sPlayerbotAIConfig.questRescueStartValleyFix, bot->GetGUIDLow());\n" "the roster + cohort gate")
need_count("${rpm}" "sPlayerbotAIConfig.questRescueStartValleyFix" 2 "key reads in RandomPlayerbotMgr.cpp (gate + teleport record)")
need_before("${targets}" "bool const valleyFix = startZoneRules" "if (startZoneRules && ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs" "gate before the HomeHubs block")

# 2. The goblin pushes: key 0 unchanged (both points, in order); with the key the rescue distance (distance only).
need("${targets}" "        if (!valleyFix)\n        {\n            targets.push_back(WorldLocation(1, gi::PortMachineX, gi::PortMachineY, gi::PortMachineZ, 0.0f));\n            targets.push_back(WorldLocation(1, gi::RazorHillX, gi::RazorHillY, gi::RazorHillZ, 0.0f));\n            return targets;\n        }\n" "the legacy goblin pushes for key 0")
need("${targets}" "            if (bot->GetMapId() == durotar.mapid && botPos.distance(WorldPosition(durotar)) <= ai::quest_search::RescueMinDistance)\n                ++nearCount;\n            else\n                targets.push_back(durotar);\n" "the goblin pushes beyond the rescue distance")
need("${targets}" "[QuestRescue] state=goblin_push bot=%u level=%u map=%u zone=%u kept=%u near=%u" "the goblin push line")

# 3. The start-valley branch with the key: own race only, the policy check (distance, dedupe, last target), its line, its return
#    before the legacy loop; after the goblin branch (goblins < 11 never reach it) and after the Z1a continent values.
need("${targets}" "    if (valleyFix && level <= ai::quest_search::StartAreaMaxLevel)\n" "the start-valley branch with the key")
need("${targets}" "if (!info || Player::TeamForRace(race) != bot->GetTeam() || !rv::TakesValleyOf(true, race, bot->getRace()))" "own race only (a)")
need("${targets}" "rv::Drop const drop = rv::Check(point, botDistance, ai::quest_search::RescueMinDistance, kept, hasLast, last);" "dedupe + distance + last target (b, c)")
need("${targets}" "bool const hasLast = rescueAi && rescueAi->ValleyRescueWithoutProgress();" "the last target only while no progress")
need("${targets}" "[QuestRescue] state=start_valley bot=%u level=%u race=%u map=%u zone=%u kept=%u near=%u duplicate=%u last=%u" "the reason line")
need_before("${targets}" "state=goblin_push" "if (valleyFix && level <= ai::quest_search::StartAreaMaxLevel)" "goblin branch before the start-valley branch")
need_before("${targets}" "bool const offHome = homeContinent && rescueMap != bot->GetMapId();" "if (valleyFix && level <= ai::quest_search::StartAreaMaxLevel)" "Z1a values before the branch")
need_before("${targets}" "tally.last);\n        return targets;\n    }\n" "            if (own && info->areaId == own->areaId && !offHome)" "the branch returns before the legacy loop")

# 4. Key 0: the legacy start-valley branch and the random pick are unchanged.
need("${targets}" "        if (level <= ai::quest_search::StartAreaMaxLevel)\n        {\n            if (own && info->areaId == own->areaId && !offHome)\n                continue;\n" "the legacy start-valley branch")
need("${targets}" "            targets.push_back(WorldLocation(info->mapId, info->positionX, info->positionY, info->positionZ, info->orientation));\n            continue;\n" "the legacy valley push")
need("${process}" "        WorldLocation const& target = targets[urand(0, uint32(targets.size()) - 1)];\n" "the random pick (unchanged)")

# 5. The last target: recorded after a started teleport only (not in QuestRescueTargets, which also runs without a teleport), roster
#    + cohort; cleared by the bot's own update on progress.
need("${process}" "        if (rescued && IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&\n                ai::cohort::Applies(sPlayerbotAIConfig.questRescueStartValleyFix, bot->GetGUIDLow()))\n            ai->MarkValleyRescue(ai::rescue_valley::Point{ target.mapid, target.coord_x, target.coord_y });\n" "the record after a started teleport")
need_before("${process}" "bool const rescued = bot->TeleportTo(" "ai->MarkValleyRescue(" "record after the teleport")
need_count("${rpm}" "MarkValleyRescue(" 1 "one record site")
need("${ai_h}" "    void MarkValleyRescue(ai::rescue_valley::Point const& target)\n    {\n        lastValleyRescue = target;\n        valleyBaselineDue = true;\n        valleyRescuePending = true;\n    }" "the record")
need("${ai_h}" "std::atomic<bool> valleyRescuePending{ false };" "the flag default false")
need("${ai_cpp}" "            if (valleyRescuePending.load())\n" "the clear only while a rescue is recorded")
need("${ai_cpp}" "                if (valleyBaselineDue.exchange(false))\n                    valleyMemory.Restart();\n                ai::rescue_valley::Moved const moved = valleyMemory.Observe(valleyNow);\n                if (moved != ai::rescue_valley::Moved::None)\n                {\n                    valleyRescuePending = false;\n" "the clear on progress against the bot-thread baseline")
need("${ai_cpp}" "            uint64 const snapshotLevelXp = snapshot;" "the level + XP part of the snapshot")
need("${ai_cpp}" "                valleyNow.gather = snapshot - snapshotLevelXp;\n" "the gathering term")
need("${ai_cpp}" "[QuestRescue] state=valley_last_cleared bot=%u level=%u moved=%s" "the clear line")
need_count("${ai_cpp}" "valleyMemory." 2 "the memory is used in the bot's own update only")
need("${ai_h}" "    ai::rescue_valley::Memory valleyMemory;   // bot thread only" "the memory member")
need_count("${rpm}" "valleyMemory" 0 "the world thread never touches the memory")

# 6. The policy: the dedupe distance and the drop order (near, duplicate, last).
need("${policy}" "constexpr float DedupeYards = 50.0f;" "the dedupe distance")
need_before("${policy}" "return Drop::Near;" "return Drop::Duplicate;" "near before duplicate")
need_before("${policy}" "return Drop::Duplicate;" "return Drop::Last;" "duplicate before last")

# 7. Key wiring: member default 0, read default 0 with the 0..3 clamp, the echo in sort order, one dist line = 0.
need("${config_h}" "    uint32 questRescueStartValleyFix = 0;" "member default 0")
need("${config_cpp}" "    questRescueStartValleyFix = config.GetIntDefault(\"AiPlayerbot.QuestRescue.StartValleyFix\", 0);\n    if (questRescueStartValleyFix > 3)\n    {\n        sLog.outError(\"[QuestRescue] config key=StartValleyFix value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", questRescueStartValleyFix);\n        questRescueStartValleyFix = 0;\n    }" "read with default 0 and clamp")
need("${config_cpp}" "            {\"QuestRescue.NoTargetFreeSlot\", Value(questRescueNoTargetFreeSlot)},\n            {\"QuestRescue.StartValleyFix\", Value(questRescueStartValleyFix)},\n            {\"QuestTurnIn.RepeatableAutoComplete\", Value(questTurnInRepeatableAutoComplete)}," "[ConfigEcho] entry in sort order")
need("${dist}" "\nAiPlayerbot.QuestRescue.StartValleyFix = 0\n" "dist default 0")
need_count("${dist}" "AiPlayerbot.QuestRescue.StartValleyFix =" 1 "dist lines")
