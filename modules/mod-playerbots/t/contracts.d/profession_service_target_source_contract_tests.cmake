# twow-repo#333 Berufe P1-v (OB-00 26.10.2026, M1r run 3: 20 trainer requests, 0 targets, every end "lost trip_s=0").
# (a) No switch: a profession trip builds its destination list without the fight gate of GetPartitions / IsLocationLevelValid
#     (bot level - 2 - deaths against the area level) and says "[ProfTravel] state=no_target" when the list gives no target.
# (b) AiPlayerbot.Travel.RpgAreaLevelFromSpawn (modes 0/1/2, default 0, ConfigEcho): RpgTravelDestination::IsPossible reads the
#     area level at the spawn nearest to the bot instead of GuidPosition(HIGHGUID_UNIT, entry) - map 0 at the origin.
# The rule behaviour is tested in t/rpg_area_level_policy_tests.cpp; this contract pins the wiring.

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
    message(FATAL_ERROR "#333 P1-v service target: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-v service target: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-v service target: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("RpgAreaLevelPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.h" mgr_h)
read_source("TravelMgr.cpp" mgr_cpp)
read_source("strategy/values/TravelValues.h" values_h)
read_source("strategy/values/TravelValues.cpp" values_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)

# (a) The profession request: its own list, no GetPartitions (whose IsLocationLevelValid gate dropped every trainer).
require_text("${choose_cpp}" "return ProfessionTripPartitions(center, partitions, travelInfo, purpose, entries, homeZone);" "the request's own list")
forbid_text("${choose_cpp}" "sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)purpose, entries, false)" "the request through GetPartitions")
require_text("${values_cpp}" "PartitionedTravelList ai::ProfessionTripPartitions(" "the list builder")
require_text("${values_cpp}" "std::pair<uint32, std::vector<WorldPosition*>> const range = destination->GetClosestPartition(center, partitions);" "the closest partition, as GetPartitions")
require_text("${values_cpp}" "sTravelMgr.GetDestinations(info, uint32(purpose), entries, false, maxDistance)" "GetPartitions' distance cap on the destinations (pre-review)")
require_text("${values_cpp}" "            if (distance > maxDistance)\n                continue;" "and on the points")
require_order("${values_cpp}" "PartitionedTravelList ai::ProfessionTripPartitions(" "    sTravelMgr.GetPartitionsLock(false);\n    return list;" "the partitions lock released")
require_order("${values_cpp}" "PartitionedTravelList ai::ProfessionTripPartitions(" "{ TerrainManager::GetZoneIdByAreaFlag(position->getAreaFlag(), position->getMapId()) })))" "the P1-u point filter inside the builder")
string(FIND "${values_cpp}" "PartitionedTravelList ai::ProfessionTripPartitions(" builder_at)
string(FIND "${values_cpp}" "void ai::ProfessionTripNoTarget(" no_target_at)
math(EXPR builder_len "${no_target_at} - ${builder_at}")
string(SUBSTRING "${values_cpp}" ${builder_at} ${builder_len} builder)
forbid_text("${builder}" "IsLocationLevelValid" "the fight gate in the profession list")

# (a) No target: one line with the reject counts, the open trip ends at once.
require_text("${choose_cpp}" "ProfessionTripNoTarget(ai, futureTravelPurpose, uint32(destinationList.size()), points, lastRejects.Format());" "the no_target call")
require_text("${choose_cpp}" "futureTravelPurpose == \"tool vendor near\") &&\n            !lastRejects.movedAway)" "not when the bot moved away (pre-review)")
require_order("${choose_cpp}" "ProfessionTripNoTarget(ai, futureTravelPurpose," "SET_AI_VALUE2(bool, \"no active travel destinations\", futureTravelPurpose, true);" "in the failure branch")
require_text("${values_cpp}" "sLog.outBasic(\"[ProfTravel] state=no_target kind=%s bot=%u level=%u cohort=%c ranges=%u points=%u path=%s rejects=%s\"," "the no_target line")
require_text("${values_cpp}" "sLog.outBasic(\"[ProfTravel] state=end kind=%s bot=%u level=%u cohort=%c reason=no_target trip_s=0 hold_over_s=0 path=%s\"," "the end at once")

# (b) The switch: modes 0/1/2, default 0, ConfigEcho, read on the bot thread into PlayerTravelInfo.
require_text("${config_h}" "uint32 travelRpgAreaLevelFromSpawn = 0;" "default off")
require_text("${config_cpp}" "travelRpgAreaLevelFromSpawn = config.GetIntDefault(\"AiPlayerbot.Travel.RpgAreaLevelFromSpawn\", 0);\n    if (travelRpgAreaLevelFromSpawn > 2)\n        travelRpgAreaLevelFromSpawn = 0;" "an unknown mode is off")
require_text("${config_cpp}" "{\"Travel.RpgAreaLevelFromSpawn\", Value(travelRpgAreaLevelFromSpawn)}," "ConfigEcho")
require_text("${conf_dist}" "AiPlayerbot.Travel.RpgAreaLevelFromSpawn = 0" "documented, default 0")
require_text("${mgr_cpp}" "rpgAreaFromSpawn = ai::cohort::Applies(sPlayerbotAIConfig.travelRpgAreaLevelFromSpawn, player->GetGUIDLow());" "cohort mode on the bot thread")
require_text("${mgr_h}" "bool UsesRpgAreaFromSpawn() const { return rpgAreaFromSpawn; }" "the snapshot bit")

# (b) IsPossible: off = the old origin read; on = the nearest spawn; the rule in the policy.
require_text("${mgr_cpp}" "    if (info.UsesRpgAreaFromSpawn())\n    {" "the switch branch")
require_text("${mgr_cpp}" "std::size_t const nearest = ai::rpg_area_level::NearestSpawn(onBotMap, sqDistances);" "the nearest spawn")
require_text("${mgr_cpp}" "destAreaLevel = spawns[nearest]->getAreaLevelOrParent();" "its area level (or the zone's)")
require_text("${mgr_cpp}" "    else\n        destAreaLevel = GuidPosition(HIGHGUID_UNIT, GetEntry()).getAreaLevel();" "off: as before")
require_text("${mgr_cpp}" "if (ai::rpg_area_level::TooHighForBot(destAreaLevel, info.GetLevel()))\n        return false;" "the rule")
require_text("${policy}" "return areaLevel > 0 && areaLevel > std::int32_t(botLevel) + kAboveBotLevels;" "more than 5 above, unknown never refuses")

# (b) CLI-Deep: the trip line per RPG trip with purpose and arm, only with a mode set; the origin level once.
require_text("${choose_cpp}" "    if (sPlayerbotAIConfig.travelRpgAreaLevelFromSpawn)\n        if (RpgTravelDestination const* rpg = dynamic_cast<RpgTravelDestination const*>(newTarget.GetDestination()))" "only with a mode set, only RPG targets")
require_text("${choose_cpp}" "sLog.outBasic(\"[RpgAreaLevel] state=trip purpose=%s bot=%u level=%u cohort=%c arm=%u entry=%d zone=%u target_area_level=%d distance=%.0f\"," "the trip line")
require_text("${choose_cpp}" "sLog.outBasic(\"[RpgAreaLevel] state=origin area_level=%d\", GuidPosition(HIGHGUID_UNIT, 1).getAreaLevel());" "the origin level once")
require_order("${choose_cpp}" "[RpgAreaLevel] state=trip" "    setNewTarget(requester, &newTarget, travelTarget);" "before the target is set")

message(STATUS "#333 P1-v service target source contract passed")
