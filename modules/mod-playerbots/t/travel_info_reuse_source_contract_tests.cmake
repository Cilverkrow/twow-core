# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A10, cards 9/10): behind AiPlayerbot.Perf.TravelInfoReuse (default 0) SetBestTarget
# builds one PlayerTravelInfo per choice (on first use), reuses a known IsActive answer of the same choice and
# checks the cheap local-hub conditions before IsActive; TravelMgr::GetDestinations looks exactly one asked
# entry up in the purpose map instead of walking every key and computes DistanceTo once. Switch 0 keeps the
# old path verbatim.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A10: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A10: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A10: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_length)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A10: ${description}: '${needle}' found ${count} times, expected ${expected}")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A10: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A10: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("TravelMgr.cpp" travel_cpp)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# 1. Switch: default off everywhere, documented with 0.
require_text("${config_h}" "bool perfTravelInfoReuse = false;" "member default off")
require_text("${config_cpp}" "perfTravelInfoReuse = config.GetBoolDefault(\"AiPlayerbot.Perf.TravelInfoReuse\", false);" "config read, default false")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.TravelInfoReuse = 0\n" "documented key with value 0")
forbid_text("${conf_dist}" "AiPlayerbot.Perf.TravelInfoReuse = 1" "shipped as on")

region("${choose}" "bool ChooseTravelTargetAction::SetBestTarget(" "\nstd::vector<std::string> split(" best)
region("${best}" "if (preferLocalQuest && !target->IsForced() && !resumeSkip)" "for (auto& [partition, travelPointList] : partitionedList)" preloop)
region("${best}" "if (!target->IsForced() && hasActiveLocalQuestHub)" "if (distanceCheck)" hub)

# 2. One switch read and one local info per choice; no static cache or snapshot, no locks.
require_text("${best}" "bool const travelInfoReuse = sPlayerbotAIConfig.perfTravelInfoReuse;" "switch read once per choice")
forbid_text("${best}" "static bool const travelInfoReuse" "static switch snapshot (ignores .reload config)")
require_text("${best}" "std::optional<PlayerTravelInfo> choiceTravelInfo;" "local per-choice info")
forbid_text("${best}" "static std::optional<PlayerTravelInfo>" "static info cache")
forbid_text("${best}" "static PlayerTravelInfo" "static info cache")
forbid_text("${best}" "shared_mutex" "shared_mutex")
require_order("${best}" "std::optional<PlayerTravelInfo> choiceTravelInfo;" "if (preferLocalQuest && !target->IsForced() && !resumeSkip)" "info declared before the pre-loop")
require_text("${best}" "choiceTravelInfo.emplace(bot);" "info built from this bot only")

# 3. Pre-loop: old local info when off, outer break only when on, predicate unchanged.
require_text("${preloop}" "PlayerTravelInfo const& travelInfo = travelInfoReuse ? choiceInfo() : preLoopTravelInfo.emplace(bot);" "pre-loop info gated")
require_text("${preloop}" "if (travelInfoReuse && hasActiveLocalQuestHub)\n                break;" "outer break gated by the switch")
require_text("${preloop}" "IsLocalActiveQuestHubDestination(bot, destination, position, travelInfo) &&\n                    (isActive[destination] = true))" "pre-loop predicate unchanged, only true stored")
require_text("${preloop}" "if (ai::travel_choose::OverBudget(WorldTimer::getMSTimeDiffToNow(chooseStart)))" "pre-loop budget check kept")

# 4. A known false answer is rejected before the hub filter and the main check (reuse relies on it).
set(known_inactive "if (!target->IsForced() && isActive.find(destination) != isActive.end() && !isActive[destination])")
require_text("${best}" "${known_inactive}" "known-inactive reject kept")
require_order("${best}" "${known_inactive}" "if (!target->IsForced() && hasActiveLocalQuestHub)" "known-inactive reject before the hub filter")

# 5. Hub filter: gated; cheap checks before IsActive; only true answers stored; old path kept when off.
require_order("${hub}" "if (travelInfoReuse)" "IsLocalQuestHubCandidate(bot, destination, position)" "new hub path gated")
require_order("${hub}" "IsLocalQuestHubCandidate(bot, destination, position)" "isActive.find(destination)" "cheap checks before the known answer")
require_order("${hub}" "IsLocalQuestHubCandidate(bot, destination, position)" "destination->IsActive(bot, choiceInfo())" "cheap checks before IsActive")
require_order("${hub}" "if (hubActive) // only a true answer is kept" "isActive[destination] = true;" "only a true hub answer kept")
count_text("${hub}" "isActive[destination] =" 1 "exactly one map write in the hub filter")
forbid_text("${hub}" "= hubActive;" "false hub answer stored (changes the reject counters)")
forbid_text("${hub}" "isActive.emplace(" "map write bypassing the true-only rule")
forbid_text("${hub}" "isActive.insert(" "map write bypassing the true-only rule")
count_text("${hub}" "++lastRejects.hubFilter;" 2 "hub reject counted on both paths")
require_order("${hub}" "IsLocalQuestHubCandidate(bot, destination, position)" "else\n                {\n                    PlayerTravelInfo travelInfo(bot);\n                    if (!IsLocalActiveQuestHubDestination(bot, destination, position, travelInfo))" "old hub path in the else branch")

# 6. Main activation check: reuse when on, old expression when off.
require_text("${best}" "if (target->IsForced() || (travelInfoReuse ? reuseActive(destination) : (isActive[destination] = destination->IsActive(bot, PlayerTravelInfo(bot)))))" "main check gated, old expression kept")
require_text("${best}" "return isActive[dest] = dest->IsActive(bot, choiceInfo());" "miss computes and stores")
require_order("${best}" "auto const known = isActive.find(dest);" "return isActive[dest] = dest->IsActive(bot, choiceInfo());" "known answer before the recompute")

# 7. Helpers: the cheap part has no IsActive and the same radius check; the old predicate keeps IsActive.
region("${choose}" "bool IsLocalQuestHubCandidate(" "\n}\n" cand)
require_text("${cand}" "position->distance(WorldPosition(bot)) <= sPlayerbotAIConfig.questFirstProgressionLocalHubRadius;" "same radius check")
require_text("${cand}" "position->getMapId() == bot->GetMapId()" "same map check")
require_text("${cand}" "questDestination && questDestination->GetQuestId() && position" "same quest destination check")
forbid_text("${cand}" "IsActive(" "IsActive in the cheap part")
region("${choose}" "bool IsLocalActiveQuestHubDestination(" "\n}\n" oldpred)
require_text("${oldpred}" "position->distance(WorldPosition(bot)) <= sPlayerbotAIConfig.questFirstProgressionLocalHubRadius &&\n        destination->IsActive(bot, travelInfo);" "old predicate unchanged")

# 8. GetDestinations: keyed lookup for exactly one entry, distance once, old loop kept for switch off.
region("${travel_cpp}" "DestinationList TravelMgr::GetDestinations(" "\nvoid TravelMgr::GetPartitionsLock(" dest)
require_text("${dest}" "bool const travelInfoReuse = sPlayerbotAIConfig.perfTravelInfoReuse;" "switch read once per call")
forbid_text("${dest}" "static bool const travelInfoReuse" "static switch snapshot (ignores .reload config)")
forbid_text("${dest}" "static " "static state in GetDestinations")
require_text("${dest}" "if (entries.size() == 1)\n            {\n                auto const found = entryDests.find(entries.front());" "lookup only for one entry")
forbid_text("${dest}" "if (!entries.empty())" "multi-entry lookup (changes the order)")
require_text("${dest}" "if (entries.empty() || std::find(entries.begin(), entries.end(), destEntry) != entries.end())" "same key filter for 0 or 2+ entries")
require_order("${dest}" "if (purposeFlag != (uint32)TravelDestinationPurpose::None" "if (travelInfoReuse)" "purpose filter before the gated path")
require_order("${dest}" "if (onlyPossible && !dest->IsPossible(info))" "float const distance = dest->DistanceTo(center);" "IsPossible before the distance")
require_text("${dest}" "if (maxDistance > 0 && distance > maxDistance)" "max distance filter on the new path")
require_text("${dest}" "if (distance == FLT_MAX)" "unreachable-map skip on the new path")
require_text("${dest}" "            continue;\n        }\n\n        for (auto& [destEntry, dests] : entryDests)" "gated path ends with continue right before the old loop")
require_order("${dest}" "if (travelInfoReuse)" "if (maxDistance > 0 && dest->DistanceTo(center) > maxDistance)" "old loop after the gated path")
require_text("${dest}" "if (entries.size() && std::find(entries.begin(), entries.end(), destEntry) == entries.end())" "old key filter kept")
require_text("${dest}" "if (onlyPossible && !dest->IsPossible(info))\n                    continue;\n\n                if (maxDistance > 0 && dest->DistanceTo(center) > maxDistance)\n                    continue;\n\n                if (dest->DistanceTo(center) == FLT_MAX)" "old loop body kept for switch off")

message(STATUS "travel_info_reuse source contract passed")
