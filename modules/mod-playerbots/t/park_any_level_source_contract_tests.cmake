# twow-repo#551 (owner 09.10.2026 via OB-00: "damit die Städte nicht zu voll werden, sollten die Bots
# unabhängig ihres Levels alle Gasthäuser mit 25 Bots füllen"): behind AiPlayerbot.Park.AnyLevelInn
# (default 0 = level band as before) ParkBot offers every inn of the bot's faction; ChooseSpot takes the
# nearest with room (SpotCapacity 25), capital spots only when all inns are full. [Park] state=share logs
# inn / city / here once a minute.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#551 any-level inn: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#551 any-level inn: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("RandomPlayerbotMgr.h" mgr_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

require_text("${config_h}" "bool parkAnyLevelInn = false;" "member default off")
require_text("${config_cpp}" "parkAnyLevelInn = config.GetBoolDefault(\"AiPlayerbot.Park.AnyLevelInn\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Park.AnyLevelInn = 0" "documented key")

# The switch only widens the inn list; the level band stays the default; inns before cities.
# twow-repo#551 park caps (10.10.2026): the loop also records each inn's capital zone; the picks carry the zones.
require_text("${mgr}" "if (sPlayerbotAIConfig.parkAnyLevelInn ||\n                (level + ParkLevelSlack >= inn.minLevel && level <= inn.maxLevel + ParkLevelSlack))\n            {\n                inns.push_back(inn.loc);" "switch widens the inn list")
require_order("${mgr}" "int index = pick(inns, innZones, true);" "index = pick(parkCities[team], parkCityZones[team], false);" "inns before capital spots")
require_order("${mgr}" "via = \"inn\";\n            ++parkShare.inn;" "via = \"city\";\n            ++parkShare.city;" "share counted")
require_text("${mgr}" "++parkShare.here;\n                return ParkBot(bot, \"here\", reason);" "fallback counted")

# Minute line in ProcessParkedBots.
require_text("${mgr_h}" "ParkShare parkShare;" "share member")
require_order("${mgr}" "void RandomPlayerbotMgr::ProcessParkedBots()" "\"[Park] state=share inn=%u city=%u here=%u any_level_inn=%u\"" "share line in ProcessParkedBots")

message(STATUS "park_any_level source contract passed")
