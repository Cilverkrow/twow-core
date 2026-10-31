cmake_policy(VERSION 3.16)
# twow-repo#601 S3b (the local quest hub pre-loop uses the main loop's filters; design rev. 2, OB-10 review 18.10.2026,
# Q-B2). AiPlayerbot.QuestFirstProgression.HubPreloopSameFilters, restart-only, default 0, acts only with FailureClasses.
# Pins:
#  - one shared predicate PostActiveDeferralOf for the main loop's four post-IsActive deferrals (battleground quest,
#    enemy home zone, route danger incl. the cross-map branch and the start-zone exemption, danger map), in the main
#    loop's order and side-effect free (no counter, no trace, no log),
#  - SetBestTarget calls it exactly twice - the main loop (which counts and traces per reason as before) and the
#    pre-loop - and has no inline deferral left, so the two loops cannot drift apart (as CandidateSkipOf for S1),
#  - the pre-loop call: after IsLocalActiveQuestHubDestination, behind the key and FailureClasses, no counter there; the
#    pinned S1 pre-loop predicate text stays,
#  - neutral defaults: member false, conf documented with 0, never shipped as 1.
# Reads only named files below modules/mod-playerbots/src/playerbot (never a glob, never src/modules/Eluna).

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 S3b: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 S3b: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#601 S3b: order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

# The part of `text` from `start` up to the first `end` after it (both required).
function(region text start end out)
  string(FIND "${text}" "${start}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "#601 S3b: missing region start: ${start}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(LENGTH "${start}" start_length)
  string(SUBSTRING "${rest}" ${start_length} -1 tail)
  string(FIND "${tail}" "${end}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#601 S3b: missing region end: ${end}")
  endif()
  math(EXPR length "${start_length} + ${end_at}")
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR next "${at} + ${len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#601 S3b: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)

# ---- 1. The shared predicate: the four deferrals in the main loop's order, side-effect free -----------------------
require_text("${choose}" "enum class PostActiveDeferral\n{\n    None,\n    BattlegroundQuest,\n    EnemyZone,\n    CrossMap,\n    ZoneLevel,\n    DangerMap,\n    LevelGuard,   // twow-repo#601 cand4 lever (ii), RosterFarMove.LevelGuard (LevelGuardPolicy.h)\n};" "deferral reasons (+ #601 cand4 lever (ii) LevelGuard)")
region("${choose}" "PostActiveDeferral PostActiveDeferralOf(" "\n}\n" deferral_fn)
require_text("${deferral_fn}" "PostActiveDeferral PostActiveDeferralOf(PlayerbotAI* ai, Player* bot, TravelTarget const* target, TravelDestination* destination,\n    WorldPosition* position, danger_map::DangerMap::Hit& dangerHit, ai::level_guard::Hit* guardHit = nullptr)" "signature (a danger map hit handed back for the death_cluster line; #601 cand4 lever (ii): the LevelGuard hit for the main loop)")
require_text("${deferral_fn}" "if (sPlayerbotAIConfig.zoneExitSkipBattlegroundQuests && !target->IsForced())" "Z3 battleground quest gate")
require_text("${deferral_fn}" "if (ai::zone_exit::IsBattlegroundQuestSort(bgQuestTemplate->GetZoneOrSort()))\n                    return PostActiveDeferral::BattlegroundQuest;" "Z3 by quest sort")
require_text("${deferral_fn}" "if (!target->IsForced() && position && position->isEnemyHomeZoneFor(bot->GetTeam()))\n        return PostActiveDeferral::EnemyZone;" "enemy home zone")
require_text("${deferral_fn}" "if (!target->IsForced() && position && UsesQuestFirstProgression(bot) &&\n        dynamic_cast<QuestTravelDestination const*>(destination))" "route danger gate (#307)")
require_text("${deferral_fn}" "route_danger::Reason const danger = route_danger::Classify(position->getMapId() != bot->GetMapId()," "route danger classification")
require_text("${deferral_fn}" "if (danger == route_danger::Reason::CrossMap)\n            return PostActiveDeferral::CrossMap;" "cross-map branch as before")
require_text("${deferral_fn}" "if (danger == route_danger::Reason::TargetZoneLevel &&\n            !ai::start_zone::ZoneLevelExempt(sPlayerbotAIConfig.customRaceStartZones, bot->getRace(), bot->GetLevel(),\n                position->getMapId(), position->getX(), position->getY()))\n            return PostActiveDeferral::ZoneLevel;" "zone level with the start-zone exemption (#575)")
require_text("${deferral_fn}" "if (!target->IsForced() && position && sPlayerbotAIConfig.dangerMapEnabled &&\n        UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster() && position->getMapId() == bot->GetMapId())" "danger map gate (#307, roster bots on their own)")
require_text("${deferral_fn}" "if (dangerHit.cells)\n            return PostActiveDeferral::DangerMap;" "danger map hit")
require_order("${deferral_fn}" "return PostActiveDeferral::BattlegroundQuest;" "return PostActiveDeferral::EnemyZone;" "order: battleground quest first")
require_order("${deferral_fn}" "return PostActiveDeferral::EnemyZone;" "return PostActiveDeferral::CrossMap;" "order: enemy zone, then route danger")
require_order("${deferral_fn}" "return PostActiveDeferral::CrossMap;" "return PostActiveDeferral::ZoneLevel;" "order: cross map, then zone level")
require_order("${deferral_fn}" "return PostActiveDeferral::ZoneLevel;" "return PostActiveDeferral::DangerMap;" "order: route danger, then danger map")
require_order("${deferral_fn}" "return PostActiveDeferral::DangerMap;" "return PostActiveDeferral::None;" "order: None last")
foreach(side_effect "lastRejects" "++" "TellDebug(" "sLog." "deathCluster" "SET_AI_VALUE" "static ")
  forbid_text("${deferral_fn}" "${side_effect}" "side effect in the shared predicate (the pre-loop must not count or trace)")
endforeach()

# ---- 2. SetBestTarget: exactly two call sites of the same function, no inline deferral left ---------------------
region("${choose}" "bool ChooseTravelTargetAction::SetBestTarget(" "\nstd::vector<std::string> split(" best)
region("${best}" "if (preferLocalQuest && !target->IsForced() && !resumeSkip)" "for (auto& [partition, travelPointList] : partitionedList)" preloop)
string(FIND "${best}" "for (auto& [partition, travelPointList] : partitionedList)" main_at)
string(SUBSTRING "${best}" ${main_at} -1 main_loop)
count_text("${best}" "PostActiveDeferralOf(ai, bot, target, destination, position, " 2 "exactly the pre-loop and the main loop, same function and arguments")
count_text("${choose}" "PostActiveDeferralOf(" 3 "the definition plus the two SetBestTarget call sites")
count_text("${preloop}" "PostActiveDeferralOf(" 1 "one pre-loop call")
count_text("${main_loop}" "PostActiveDeferralOf(" 1 "one main-loop call")
foreach(inline "IsBattlegroundQuestSort(" "isEnemyHomeZoneFor(" "route_danger::Classify(" "ZoneLevelExempt(" "danger_map::Instance().Query(" "zoneExitSkipBattlegroundQuests" "dangerMapEnabled")
  forbid_text("${best}" "${inline}" "an inline post-IsActive deferral in SetBestTarget (the two loops would drift apart)")
endforeach()

# ---- 3. Main loop: right after IsActive, before the range skip; same counters and traces per reason --------------
require_text("${main_loop}" "PostActiveDeferral const deferral = PostActiveDeferralOf(ai, bot, target, destination, position, hit, &guardHit);" "main loop asks the shared predicate (#601 cand4 lever (ii): with the LevelGuard out parameter)")
require_order("${main_loop}" "if (target->IsForced() || (travelInfoReuse ? reuseActive(destination)" "PostActiveDeferral const deferral = PostActiveDeferralOf(" "after the IsActive check")
require_order("${main_loop}" "PostActiveDeferral const deferral = PostActiveDeferralOf(" "ai::quest_failure_class::RangeSkipAllowed(failureClasses, hasActiveLocalQuestHub)" "before the range skip")
set(case_bg "case PostActiveDeferral::BattlegroundQuest:\n                        ++lastRejects.battlegroundQuest;\n                        continue;")
set(case_enemy "case PostActiveDeferral::EnemyZone:\n                        ai->TellDebug(requester, \"Skipping \" + destination->GetTitle() + \" - enemy home zone\", \"debug travel\");\n                        ++lastRejects.enemyZone;\n                        continue;")
set(case_cross "case PostActiveDeferral::CrossMap:\n                        ++deferredCrossMap;\n                        ++lastRejects.crossMap;\n                        continue;")
set(case_zone "case PostActiveDeferral::ZoneLevel:\n                        ++deferredZoneLevel;\n                        ++lastRejects.zoneLevel;\n                        continue;")
set(case_danger "case PostActiveDeferral::DangerMap:\n                        ++deferredDeathCluster;\n                        deathClusterCells += hit.cells;\n                        deathClusterWorstKillerLevel = std::max(deathClusterWorstKillerLevel, hit.worstKillerLevel);\n                        ++lastRejects.dangerMap;\n                        continue;")
set(case_none "case PostActiveDeferral::None:\n                        break;")
foreach(c case_bg case_enemy case_cross case_zone case_danger case_none)
  require_text("${main_loop}" "${${c}}" "main loop counts and traces as before (${c})")
endforeach()
foreach(counter "++lastRejects.battlegroundQuest;" "++lastRejects.enemyZone;" "++lastRejects.crossMap;" "++lastRejects.zoneLevel;" "++lastRejects.dangerMap;" "++deferredCrossMap;" "++deferredZoneLevel;" "++deferredDeathCluster;")
  count_text("${best}" "${counter}" 1 "counter once, in the main loop only")
endforeach()
require_text("${best}" "reason=route_danger detail=death_cluster deferred=%u cells=%u worst_killer_level=%u selected_other=%u" "death_cluster line unchanged")
require_text("${best}" "state=deferred bot=%u level=%u reason=route_danger cross_map=%u target_zone_level=%u selected_other=%u" "route danger line unchanged")

# ---- 4. Pre-loop: after the hub test, behind the key and FailureClasses, no counter or trace ---------------------
require_text("${best}" "bool const hubPreloopSameFilters = failureClasses && sPlayerbotAIConfig.questFirstProgressionHubPreloopSameFilters &&\n        sPlayerbotAIConfig.S3For(bot->GetGUIDLow());" "key effective only with FailureClasses (R01 v4: and the S3 cohort)")
count_text("${choose}" "questFirstProgressionHubPreloopSameFilters" 1 "key read once per choice")
require_order("${best}" "bool const failureClasses = preferLocalQuest && sPlayerbotAIConfig.questFirstProgressionFailureClasses &&\n        sPlayerbotAIConfig.S1For(bot->GetGUIDLow());" "bool const hubPreloopSameFilters =" "derived from the S1 switch")
require_text("${preloop}" "else if ((!failureClasses || CandidateSkipOf(ai, bot, target, persistentTarget, excludedTurnInTarget, destination,\n                        position) == CandidateSkip::None) &&\n                    IsLocalActiveQuestHubDestination(bot, destination, position, travelInfo) &&\n                    (isActive[destination] = true))" "S1 pre-loop predicate unchanged")
require_text("${preloop}" "                    danger_map::DangerMap::Hit preLoopHit;\n                    if (hubPreloopSameFilters && PostActiveDeferralOf(ai, bot, target, destination, position, preLoopHit) !=\n                            PostActiveDeferral::None)\n                        continue;\n                    hasActiveLocalQuestHub = true;\n                    break;" "a deferred local candidate keeps the hub filter off (key tested first)")
require_order("${preloop}" "IsLocalActiveQuestHubDestination(bot, destination, position, travelInfo) &&" "PostActiveDeferralOf(" "after the local hub test (IsActive passed)")
foreach(side_effect "lastRejects" "deferred" "deathCluster" "TellDebug(" "sLog.")
  forbid_text("${preloop}" "${side_effect}" "a main-loop counter or trace in the pre-loop")
endforeach()
count_text("${preloop}" "hasActiveLocalQuestHub = true;" 1 "one place switches the hub filter on")

# ---- 5. Neutral defaults ---------------------------------------------------------------------------------------
require_text("${config_h}" "    bool questFirstProgressionHubPreloopSameFilters = false;" "member default false")
require_text("${config_cpp}" "questFirstProgressionHubPreloopSameFilters = config.GetBoolDefault(\"AiPlayerbot.QuestFirstProgression.HubPreloopSameFilters\", false);" "read with default false")
require_text("${conf}" "\nAiPlayerbot.QuestFirstProgression.HubPreloopSameFilters = 0\n" "documented off")
count_text("${conf}" "\nAiPlayerbot.QuestFirstProgression.HubPreloopSameFilters" 1 "key once in the conf")
forbid_text("${conf}" "AiPlayerbot.QuestFirstProgression.HubPreloopSameFilters = 1" "shipped as on")
require_order("${conf}" "\nAiPlayerbot.QuestFirstProgression.FailureClasses = 0\n" "\nAiPlayerbot.QuestFirstProgression.HubPreloopSameFilters = 0\n" "documented next to the S1 switches it needs")

message(STATUS "hub_preloop_same_filters_source_contract: all pins hold")
