# twow-repo#485: quest givers and takers a bot cannot use are no travel targets.
# Paths only through PB_SOURCE_DIR (never CMAKE_SOURCE_DIR: twow-repo builds the
# core under /src/core).
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

# Fails unless first, second and third occur in this order within max_span characters.
function(require_order text first second third max_span description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  string(FIND "${text}" "${third}" third_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR third_at EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${first} / ${second} / ${third}")
  endif()
  if(NOT first_at LESS second_at OR NOT second_at LESS third_at)
    message(FATAL_ERROR "Wrong order for ${description}: ${first} / ${second} / ${third}")
  endif()
  math(EXPR span "${third_at} - ${first_at}")
  if(span GREATER max_span)
    message(FATAL_ERROR "Too far apart (${span} > ${max_span}) for ${description}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/ProgressAwareTurnInRecoveryPolicy.h" recovery)
file(READ "${PB_SOURCE_DIR}/RouteDangerPolicy.h" route_danger)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)
file(READ "${PB_SOURCE_DIR}/WorldSquare.h" world_square)

# (1) Bug fix without a switch: IsOverWorld() checks the taker's map, so an elite or
# dungeon taker inside an instance (5722, Ragefire Chasm) is the one a bot without
# "can fight boss" skips - as the objective checks do.
string(FIND "${travel_mgr}" "bool QuestRelationTravelDestination::IsPossible(" relation_at)
if(relation_at EQUAL -1)
  message(FATAL_ERROR "Missing QuestRelationTravelDestination::IsPossible")
endif()
string(SUBSTRING "${travel_mgr}" ${relation_at} -1 relation_text)
string(FIND "${relation_text}" "//Do not try to hand-in dungeon/elite quests in instances without a group." hand_in_at)
if(hand_in_at EQUAL -1)
  message(FATAL_ERROR "Missing hand-in comment in QuestRelationTravelDestination::IsPossible")
endif()
string(SUBSTRING "${relation_text}" ${hand_in_at} 300 hand_in_window)
require_text("${hand_in_window}" "if (!IsOverWorld(info.GetPosition()))" "hand-in skips an instance taker")
reject_text("${hand_in_window}" "if (IsOverWorld(info.GetPosition()))" "inverted hand-in instance check")

# (2) Script-only game objects (gameobject.spawntimesecsmin < 0, GO 270 of quest 310)
# get no quest giver/taker points while the quest travel table is built; objectives
# keep them.
string(FIND "${travel_mgr}" "void TravelMgr::LoadQuestTravelTable()" load_at)
if(load_at EQUAL -1)
  message(FATAL_ERROR "Missing TravelMgr::LoadQuestTravelTable")
endif()
string(SUBSTRING "${travel_mgr}" ${load_at} -1 load_text)
string(FIND "${load_text}" "sLog.outString(\"Loading all travel locations.\");" load_end)
if(load_end EQUAL -1)
  message(FATAL_ERROR "Missing end of the quest destination loop in LoadQuestTravelTable")
endif()
string(SUBSTRING "${load_text}" 0 ${load_end} quest_table)
require_text("${quest_table}" "sPlayerbotAIConfig.questFirstProgressionSkipScriptOnlyQuestTakers && entry < 0" "switch, game objects only")
require_text("${quest_table}" "goData && goData->spawntimesecsmin < 0" "not spawned by default")
require_text("${quest_table}" "dynamic_cast<QuestRelationTravelDestination*>(tLoc)" "quest givers and takers only")
require_text("${quest_table}" "[QuestFirstRoute] state=taker_script_only quest=%u entry=%d skipped_points=%u" "visible startup record")
require_text("${config_source}" "\"AiPlayerbot.QuestFirstProgression.SkipScriptOnlyQuestTakers\", false" "off by default")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.SkipScriptOnlyQuestTakers = 0" "documented key")

# Such a giver keeps its destination without points: the off-continent check in
# QuestRelationTravelDestination::IsActive (reached by "print travel") and the
# travel_destinations.csv export must not dereference a missing point or square.
string(FIND "${travel_mgr}" "bool QuestRelationTravelDestination::IsActive(" active_at)
if(active_at EQUAL -1)
  message(FATAL_ERROR "Missing QuestRelationTravelDestination::IsActive")
endif()
string(SUBSTRING "${travel_mgr}" ${active_at} 1500 active_window)
require_text("${active_window}" "if (!closestPoint || closestPoint->getMapId() != bot->GetMapId())" "giver without points is no crash")
reject_text("${travel_mgr}" "(GetClosestPoint(bot)->getMapId() != bot->GetMapId())" "unchecked closest point")
require_text("${world_square}" "return subSquares.empty() ? 0 : subSquares.begin()->second.GetSize();" "size of an empty destination")
reject_text("${world_square}" "{ return subSquares.begin()->second.GetSize(); }" "unchecked first sub-square")

# (3) Bug fix without a switch: a stall suppression is keyed by the target's map, the
# key IsTurnInRouteSuppressed is asked with (cross-map turn-ins).
require_text("${travel_mgr}" "observation.targetMapId = wPosition->getMapId();" "target map observed")
require_text("${recovery}" "state.suppressedMapId = current.targetMapId;" "stall suppression keyed by the target map")
reject_text("${recovery}" "state.suppressedMapId = current.mapId;" "stall suppression keyed by the bot map")

# Critic B2.2: the death cooldown (#307) has its own slot; a stall cannot overwrite it.
require_text("${recovery}" "state.deathSuppressedUntil = now + cooldownMs;" "death cooldown in its own slot")
require_text("${recovery}" "state.deathSuppressedUntil > now" "suppression check sees the death cooldown")
reject_text("${recovery}" "state.suppressUntil = now + cooldownMs;" "death cooldown in the stall slot")

# Critic B2.3: CrossMapContinentsOnly limits MinLevelForCrossMapQuestRoute to a
# route to the other continent. Both ends go through ContinentOf (the tram 369 is
# the Eastern Kingdoms, an instance the continent of its ghost entrance), so a
# tram or instance route on one continent stays open and a switch from it does not.
require_text("${route_danger}" "inline bool IsContinentSwitch(std::uint32_t fromMapId, std::uint32_t toMapId)" "continent switch helper")
require_text("${route_danger}" "inline std::uint32_t ContinentOf(std::uint32_t mapId, std::int32_t ghostEntranceMapId)" "continent of tram and instances")
require_text("${route_danger}" "(!continentsOnly || continentSwitch)" "continents-only cross-map rule")
require_order("${choose}" "route_danger::Classify(position->getMapId() != bot->GetMapId()"
  "sPlayerbotAIConfig.questFirstProgressionCrossMapContinentsOnly,"
  "route_danger::ContinentOf(position->getMapId(), targetMapEntry ? targetMapEntry->ghostEntranceMap : -1)" 800
  "continents-only switch passed to the route danger check")
require_text("${choose}" "route_danger::ContinentOf(bot->GetMapId(), botMapEntry ? botMapEntry->ghostEntranceMap : -1)" "continent of the bot's map")
reject_text("${choose}" "route_danger::IsContinentSwitch(bot->GetMapId(), position->getMapId())" "raw map ids as continents")
require_text("${config_source}" "\"AiPlayerbot.QuestFirstProgression.CrossMapContinentsOnly\", false" "off by default")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.CrossMapContinentsOnly = 0" "documented key")

message(STATUS "QUEST_TAKER_USABLE_CONTRACT=PASS")
