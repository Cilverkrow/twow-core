# twow-repo#541 audit A39 (owner go 10.10.2026): AiPlayerbot.DeathLoop.Escape (default 0). A death in an area of the
# hostile faction is a loop at once; the evacuation (RepopAction) never ends at a homebind in a hostile area - the
# homebind is reset to the race's safe start first. Off: the old detection and evacuation.

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
    message(FATAL_ERROR "#541 A39: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A39: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#541 A39: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#541 A39: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("DeathLoopPolicy.h" policy)
read_source("RandomPlayerbotFactory.cpp" factory)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("strategy/actions/ReleaseSpiritAction.h" repop_h)

require_text("${config_h}" "bool deathLoopEscape = false;" "switch default off")
require_text("${config_cpp}" "deathLoopEscape = config.GetBoolDefault(\"AiPlayerbot.DeathLoop.Escape\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.DeathLoop.Escape = 0" "documented")

# Detection: hostile-area death behind the switch, through the existing enemy-home-zone check.
between("${ai_cpp}" "void PlayerbotAI::RecordDeathForLoop()" "void PlayerbotAI::RecordDeathForSeries()" record)
require_order("${record}" "if (sPlayerbotAIConfig.deathLoopEscape)" "lastDeathHostileArea = WorldPosition(bot).isEnemyHomeZoneFor(bot->GetTeam());" "hostile death only with the switch")
require_text("${record}" "death_loop::RepeatAfterEscape(death.atSeconds, lastDeathLoopEscapeAt, sPlayerbotAIConfig.deathLoopWindowSeconds)" "repeat after escape counted")
between("${ai_cpp}" "bool PlayerbotAI::IsInDeathLoop() const" "\n}\n" in_loop)
require_order("${in_loop}" "if (sPlayerbotAIConfig.deathLoopEscape && lastDeathHostileArea)\n        return true;" "return death_loop::IsLoop(recentDeaths" "hostile death before the old loop check")
require_text("${ai_h}" "void ClearDeathLoop() { recentDeaths.clear(); lastDeathHostileArea = false; }" "evacuation clears the hostile flag")

# Evacuation: decided before the teleport, homebind reset when it lies in a hostile area.
between("${repop_h}" "class RepopAction : public SpiritHealerAction" "class SelfResurrectAction" repop)
require_order("${repop}" "bool const deathLoop = ai->IsInDeathLoop();" "ai->ClearDeathLoop();" "loop read before it is cleared")
require_order("${repop}" "if (sPlayerbotAIConfig.deathLoopEscape && deathLoop)" "bot->TeleportTo(defaultPlayerInfo->mapId" "escape decided before the teleport")
require_order("${repop}" "if (sPlayerbotAIConfig.deathLoopEscape && deathLoop)" "bot->TeleportToHomebind();" "rebind before the homebind teleport")
require_text("${repop}" "bool const hostileHome = home.isEnemyHomeZoneFor(bot->GetTeam());" "hostile homebind check")
require_order("${repop}" "if (hostileHome && useHomebindOverride && death_loop::RaceStartOverride(bot->getRace(), start))" "bot->SetHomebindToLocation(WorldLocation(start.map, start.x, start.y, start.z, 0.0f), start.zone);" "override races rebound to the safe start")
require_order("${repop}" "else if (hostileHome && defaultPlayerInfo)" "defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ, defaultPlayerInfo->orientation), defaultPlayerInfo->areaId);" "other races rebound to the race start")
require_text("${repop}" "[DeathLoop] state=escape guid=%u race=%u level=%u hostile_home=%u rebound=%u to_map=%u safe=%u" "escape line")

# The safe starts are the factory's.
require_text("${factory}" "WorldLocation(1, -618.518f, -4251.67f, 38.718f, 0.0f), 14" "factory goblin start")
require_text("${policy}" "out = StartPoint{ 1, -618.518f, -4251.67f, 38.718f, 14 };" "policy goblin start = factory")
require_text("${factory}" "WorldLocation(0, -8949.95f, -132.493f, 83.5312f, 0.0f), 12" "factory high elf start")
require_text("${policy}" "out = StartPoint{ 0, -8949.95f, -132.493f, 83.5312f, 12 };" "policy high elf start = factory")

# Minute line only with the switch.
require_order("${mgr}" "if (sPlayerbotAIConfig.deathLoopEscape)" "[DeathLoop] state=minute detected=%llu escaped=%llu rebound=%llu repeat_after_escape=%llu hostile_death=%llu" "minute line behind the switch")

message(STATUS "deathloop_escape source contract passed")
