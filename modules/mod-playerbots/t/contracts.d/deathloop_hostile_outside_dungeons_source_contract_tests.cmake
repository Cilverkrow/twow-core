cmake_policy(VERSION 3.16)
# twow-repo#629 G1 (CLI-GRP g1n 26.10.2026, owner go): AiPlayerbot.DeathLoop.HostileAreaOutsideDungeons (default 0).
# With DeathLoop.Escape every Alliance death in Wailing Caverns (Horde territory) was a hostile-area loop at once and
# the bot was evacuated out of the instance (19 times in one run). With the switch a death inside a dungeon or raid is
# no hostile-area death (DeathLoopPolicy.h HostileAreaDeath, unit test deathloop_escape_policy). Pins key and call site.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "deathloop hostile outside dungeons: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "deathloop hostile outside dungeons: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "deathloop hostile outside dungeons: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("DeathLoopPolicy.h" policy)
read_source("PlayerbotAI.cpp" ai_cpp)

# 1. Key: default off, documented with 0, echoed in sorted position.
require_text("${config_h}" "bool deathLoopHostileAreaOutsideDungeons = false;" "member default off")
require_text("${config_cpp}" "deathLoopHostileAreaOutsideDungeons = config.GetBoolDefault(\"AiPlayerbot.DeathLoop.HostileAreaOutsideDungeons\", false);" "default 0")
require_text("${conf_dist}" "AiPlayerbot.DeathLoop.HostileAreaOutsideDungeons = 0" "documented off")
require_order("${config_cpp}" "{\"DeathLoop.FarEscape\", Value(deathLoopFarEscape)}," "{\"DeathLoop.HostileAreaOutsideDungeons\", Value(deathLoopHostileAreaOutsideDungeons)}," "ConfigEcho after FarEscape")
require_order("${config_cpp}" "{\"DeathLoop.HostileAreaOutsideDungeons\", Value(deathLoopHostileAreaOutsideDungeons)}," "{\"DeathLoop.MaxDeaths\", Value(deathLoopMaxDeaths)}," "ConfigEcho before MaxDeaths")

# 2. Rule: off = the A39 mark unchanged.
require_text("${policy}" "    return enemyHomeZone && !(outsideDungeonsOnly && inDungeon);" "pure rule")

# 3. Call site: the only write of the mark from a death goes through the rule, inside the DeathLoop.Escape block.
require_order("${ai_cpp}" "void PlayerbotAI::RecordDeathForLoop()" "    if (sPlayerbotAIConfig.deathLoopEscape)
    {" "record function")
require_order("${ai_cpp}" "    if (sPlayerbotAIConfig.deathLoopEscape)
    {" "        lastDeathHostileArea = death_loop::HostileAreaDeath(WorldPosition(bot).isEnemyHomeZoneFor(bot->GetTeam()),
            map && map->IsDungeon(), sPlayerbotAIConfig.deathLoopHostileAreaOutsideDungeons);" "mark through the rule")
string(FIND "${ai_cpp}" "lastDeathHostileArea = WorldPosition(bot).isEnemyHomeZoneFor" raw_mark)
if(NOT raw_mark EQUAL -1)
  message(FATAL_ERROR "deathloop hostile outside dungeons: the hostile-area mark bypasses HostileAreaDeath")
endif()
