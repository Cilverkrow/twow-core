# twow-repo#551 (owner 10.10.2026, test profile 20 / 150): AiPlayerbot.Park.MaxPerInn and AiPlayerbot.Park.MaxPerCity,
# both default 0 = the old behaviour. A full inn overflows to the next inn, a full capital zone to an inn outside it;
# with a cap on, a bot with no spot is not parked (never the open world because of a cap); city counts thread-safe.

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
    message(FATAL_ERROR "#551 park caps: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#551 park caps: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#551 park caps: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#551 park caps: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("ParkPolicy.h" policy)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("RandomPlayerbotMgr.h" mgr_h)

require_text("${config_h}" "uint32 parkMaxPerInn = 0;" "MaxPerInn default 0")
require_text("${config_h}" "uint32 parkMaxPerCity = 0;" "MaxPerCity default 0")
require_text("${config_cpp}" "parkMaxPerInn = config.GetIntDefault(\"AiPlayerbot.Park.MaxPerInn\", 0);" "MaxPerInn config")
require_text("${config_cpp}" "parkMaxPerCity = config.GetIntDefault(\"AiPlayerbot.Park.MaxPerCity\", 0);" "MaxPerCity config")
require_text("${conf_dist}" "AiPlayerbot.Park.MaxPerInn = 0" "MaxPerInn documented")
require_text("${conf_dist}" "AiPlayerbot.Park.MaxPerCity = 0" "MaxPerCity documented")

# Policy: off = 25 per spot and no city limit; a full city blocks its spots; no open-world fallback with a cap.
require_text("${policy}" "return inn && maxPerInn ? maxPerInn : SpotCapacity;" "inn capacity")
require_text("${policy}" "if (cityZone && maxPerCity && cityCount >= maxPerCity)\n        return capacity;" "full city blocks its spots")
require_text("${policy}" "return !maxPerInn && !maxPerCity;" "fallback only with both caps off")
between("${policy}" "class CityCounts" "// Reduced AI tick" counts)
string(REGEX MATCHALL "std::scoped_lock lock\\(mutex\\)" count_locks "${counts}")
list(LENGTH count_locks count_lock_count)
if(NOT count_lock_count EQUAL 4)
  message(FATAL_ERROR "#551 park caps: CityCounts Add/Remove/Get/Snapshot must each lock (found ${count_lock_count})")
endif()

# ParkBot: the pick goes through the caps; no spot with a cap on = not parked, before the old fallback.
between("${mgr}" "bool RandomPlayerbotMgr::ParkBot(" "void RandomPlayerbotMgr::UnparkBot(" park)
require_text("${park}" "uint32 const capacity = ai::park::SpotCapacityFor(inns, sPlayerbotAIConfig.parkMaxPerInn);" "capacity per pick")
require_text("${park}" "occupied.push_back(ai::park::EffectiveOccupied(taken, capacity, zone, parkCityCounts.Get(zone),\n                sPlayerbotAIConfig.parkMaxPerCity));" "city cap in the pick")
require_text("${park}" "int const chosen = ai::park::ChooseSpot(ParkPoint(bot), points, occupied, capacity);" "capacity passed on")
require_text("${park}" "int index = pick(inns, innZones, true);" "inns picked as inns")
require_text("${park}" "index = pick(parkCities[team], parkCityZones[team], false);" "city spots with their zones")
require_order("${park}" "if (!ai::park::FallbackHere(sPlayerbotAIConfig.parkMaxPerInn, sPlayerbotAIConfig.parkMaxPerCity))" "state=fallback_here" "no-spot check before the old fallback")
between("${park}" "if (!ai::park::FallbackHere(" "state=fallback_here" no_spot)
require_order("${no_spot}" "[Park] state=no_spot" "return false;" "no_spot logged, not parked")
string(FIND "${no_spot}" "ParkBot(bot, \"here\"" here_in_no_spot)
if(NOT here_in_no_spot EQUAL -1)
  message(FATAL_ERROR "#551 park caps: the no-spot branch must not park in the open world")
endif()
require_order("${park}" "entry.cityZone = cityZone;\n    parkedBots[bot->GetGUIDLow()] = entry;" "parkCityCounts.Add(cityZone);" "count after the park entry")
between("${mgr}" "void RandomPlayerbotMgr::ReleaseParkSpot(ParkEntry const& entry)" "bool RandomPlayerbotMgr::ParkBot(" release)
require_text("${release}" "parkCityCounts.Remove(entry.cityZone);" "count released with the spot")
require_text("${mgr_h}" "ai::park::CityCounts parkCityCounts;" "one central counter")

# Minute line only with a cap on.
require_order("${mgr}" "if (capsOn)" "[Park] state=caps max_inn=%u max_city=%u inns_used=%u inns_full=%u inn_max=%u inn_bots=%u overflow=%u no_spot=%u cities=%s inns=%s" "caps line behind the switches")

message(STATUS "park_caps source contract passed")
