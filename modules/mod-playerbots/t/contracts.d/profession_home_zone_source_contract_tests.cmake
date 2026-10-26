# twow-repo#333 Berufe P1-u (owner 26.10.2026 via OB-00: early access to the professions yes, leaving the start area only for a
# profession no; OB-00 go X = 10): below level 10 a profession trip (trainer near and gap, tool vendor) only goes to an entry
# with a spawn in the zone the bot stands in (the zone, not the sub-area). No key: part of ProfessionTraining.Travel /
# ProfessionUse.ToolBuy (default 0). One "[ProfTravel] state=target" line names where a trip goes.
# The rule behaviour is tested in t/profession_travel_policy_tests.cpp; this contract pins the wiring.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-u home zone: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-u home zone: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("ProfessionTravelPolicy.h" policy)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/values/TravelValues.h" values_h)
read_source("strategy/values/TravelValues.cpp" values_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)

# The rule: X = 10 as a constant, the zone (not the sub-area), an unknown zone never counts.
require_text("${policy}" "constexpr std::uint32_t kHomeZoneBelowLevel = 10;" "X = 10 (OB-00 go)")
require_text("${policy}" "return level < kHomeZoneBelowLevel;" "below X only")
require_text("${policy}" "return botZone != 0 && std::find(spawnZones.begin(), spawnZones.end(), botZone) != spawnZones.end();" "a spawn in the bot's zone")
require_order("${policy}" "        return \"unreachable\";" "        return \"home_zone\";" "the gate reason after unreachable")

# The filter in the one function every profession trip goes through (near walk, gap start condition, request, tool vendor).
require_text("${values_cpp}" "bool const homeZoneOnly = ai::profession_travel::HomeZoneOnly(bot->GetLevel());" "the level rule")
require_text("${values_cpp}" "if (homeZoneOnly && !ai::profession_travel::SpawnInHomeZone(botZone, ProfessionTripSpawnZones(bot, entry, purpose)))" "the zone check of a kept entry")
require_order("${values_cpp}" "std::vector<int32> ai::ProfessionTripTargets(" "if (homeZoneOnly && !ai::profession_travel::SpawnInHomeZone(" "inside ProfessionTripTargets")
require_text("${values_cpp}" "zones.push_back(TerrainManager::GetZoneIdByAreaFlag(point->getAreaFlag(), point->getMapId()));" "the zone of a spawn, from its cached area flag")
require_text("${values_cpp}" "professionTripZones[key] = zones;" "spawn zones cached per entry, purpose and side")
require_text("${values_h}" "uint32 homeZone = 0;" "the dropped count")
require_text("${values_cpp}" "!walked || counts.trainers > 0, !walked || counts.inRadius > 0, !walked || found || drops.homeZone > 0, found);" "home_zone in the near gate")

# Per spawn, not only per entry (review 26.10.): the request's partitions keep only the points in the bot's zone.
require_text("${choose_cpp}" "uint32 const homeZone = ai::profession_travel::HomeZoneOnly(bot->GetLevel()) ? bot->GetZoneId() : 0;" "the zone of a request below level 10")
# P1-v: the request's list is ProfessionTripPartitions (TravelValues.cpp), which drops the points outside the zone.
require_text("${choose_cpp}" "return ProfessionTripPartitions(center, partitions, travelInfo, purpose, entries, homeZone);" "the request passes the zone to its list")
require_text("${values_cpp}" "if (!position || (homeZone && !ai::profession_travel::SpawnInHomeZone(homeZone," "the points outside the zone leave the list")

# Movement evidence (OB-00): the request line counts the home-zone drops, the target line names the destination.
require_text("${choose_cpp}" "drops.homeZone, bot->GetZoneId());" "dropped_home_zone and zone on the request line")
require_text("${choose_cpp}" "sLog.outBasic(\"[ProfTravel] state=target kind=%s bot=%u level=%u cohort=%c entry=%d zone=%u target_zone=%u map=%u target_map=%u distance=%.0f path=%s\"," "the target line")
require_order("${choose_cpp}" "sLog.outBasic(\"[ProfTravel] state=target" "    setNewTarget(requester, &newTarget, travelTarget);" "the target line before the target is set")
require_text("${conf_dist}" "home_zone" "documented")
require_text("${conf_dist}" "[ProfTravel] state=target" "documented")

message(STATUS "#333 P1-u home zone source contract passed")
