cmake_policy(VERSION 3.16)
# twow-repo#601/#602 G2 (CLI-Deep 26.10.2026, OB-00 go): AiPlayerbot.DeathLoop.FarEscape (default 0)
# - Goblin hunters L3-4 died 48 times at the Blackstone Island spirit healer: revive, dead 1-40 s later, ~10 min of ghost
#   time, again. The window rule (4 deaths in 900 s) fired twice, and both evacuations went to the homebind on the island,
#   380 yd away. With the switch 3 deaths in a row soon after a revive are a loop, and an evacuation point at the loop is
#   replaced by the race's safe start. Pins the key, the revive stamps, the record at death, the loop check, the escape.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "deathloop far escape: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "deathloop far escape: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "deathloop far escape: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "deathloop far escape: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "deathloop far escape: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("DeathLoopPolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/actions/ReviveFromCorpseAction.cpp" revive)
read_source("strategy/actions/ReleaseSpiritAction.h" release)

# 1. Key: neutral default, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 deathLoopFarEscape = 0;" "member default off")
require_text("${config_cpp}" "deathLoopFarEscape = config.GetIntDefault(\"AiPlayerbot.DeathLoop.FarEscape\", 0);" "default 0")
require_text("${config_cpp}" "if (deathLoopFarEscape > 2)" "unknown mode checked")
require_text("${config_cpp}" "{\"DeathLoop.FarEscape\", Value(deathLoopFarEscape)}," "ConfigEcho entry")
require_text("${config_cpp}" "{\"DeathLoop.WindowSeconds\", Value(deathLoopWindowSeconds)}," "ConfigEcho of the window")
require_text("${conf_dist}" "\nAiPlayerbot.DeathLoop.FarEscape = 0\n" "documented")

# 2. Policy: the agreed constants (OB-00 / CLI-ROT 26.10.: S60 / N3, escape point at the loop below 600 yd).
require_text("${policy}" "constexpr std::uint32_t ShortLifeSeconds = 60;" "short life seconds")
require_text("${policy}" "constexpr std::uint32_t ShortLifeDeaths = 3;" "short life deaths")
require_text("${policy}" "constexpr float FarEscapeMinYards = 600.0f;" "far escape distance")

# 3. Revive stamps: corpse (only when the reclaim worked) and spirit healer.
between("${revive}" "HandleReclaimCorpseOpcode(packet);" "SET_AI_VALUE(bool, \"corpse run\", false);" corpse)
require_text("${corpse}" "if (!sServerFacade.UnitIsDead(bot))\n        ai->RecordReviveForLoop();" "corpse revive stamped when alive")
require_order("${revive}" "bot->ResurrectPlayer(0.5f, !ai->HasCheat(BotCheatMask::repair));" "ai->RecordReviveForLoop();   // twow-repo#601/#602 G2" "spirit healer revive stamped")

# 4. Record at death: switch + cohort, latest death kept, short life streak; the old record untouched.
between("${ai_cpp}" "void PlayerbotAI::RecordDeathForLoop()" "\n}\n" record)
require_order("${record}" "death_loop::Record(recentDeaths, death, DeathLoopSettings());" "if (ai::cohort::Applies(sPlayerbotAIConfig.deathLoopFarEscape, bot->GetGUIDLow()))" "old record first")
require_text("${record}" "death_loop::RecordShortLifeDeath(shortLife, death.atSeconds, death.mapId, death.x, death.y, sPlayerbotAIConfig.deathLoopRadius)" "short life uses the loop radius")
require_text("${record}" "[DeathLoop] state=short_life guid=%u" "greppable short life line")

# 5. Loop check: after the hostile death, before the window rule; switch-gated; the clear resets the streak.
between("${ai_cpp}" "bool PlayerbotAI::IsInDeathLoop() const" "\n}\n" in_loop)
require_order("${in_loop}" "if (sPlayerbotAIConfig.deathLoopEscape && lastDeathHostileArea)" "if (death_loop::IsShortLifeLoop(shortLife) && uint32(time(nullptr)) - lastLoopDeath.atSeconds <= sPlayerbotAIConfig.deathLoopWindowSeconds &&\n        ai::cohort::Applies(sPlayerbotAIConfig.deathLoopFarEscape, bot->GetGUIDLow()))" "hostile death first; the streak expires with the window")
require_order("${in_loop}" "death_loop::IsShortLifeLoop(shortLife)" "return death_loop::IsLoop(recentDeaths" "short life before the window rule")
require_text("${ai_h}" "void ClearDeathLoop() { recentDeaths.clear(); lastDeathHostileArea = false; shortLife = ai::death_loop::ShortLife(); }" "evacuation clears the streak")

# 6. Escape: after the A39 block, before the teleports; at the loop -> safe start, homebind set, homebind branch.
between("${release}" "class RepopAction : public SpiritHealerAction" "class SelfResurrectAction" repop)
require_order("${repop}" "[DeathLoop] state=escape guid=%u" "[DeathLoop] state=far_escape guid=%u" "after the A39 escape block")
require_order("${repop}" "[DeathLoop] state=far_escape guid=%u" "bot->TeleportToHomebind();" "before the teleports")
require_text("${repop}" "if (deathLoop && ai::cohort::Applies(sPlayerbotAIConfig.deathLoopFarEscape, bot->GetGUIDLow()) && ai->GetLatestLoopDeath(loopDeath, loopZone))" "gate")
require_text("${repop}" "bool known = death_loop::RaceStartOverride(bot->getRace(), start);" "goblin / high elf safe start first")
require_text("${repop}" "!death_loop::EscapeAtLoop(loopDeath.mapId, loopDeath.x, loopDeath.y, loopZone, safeStart.mapid, safeStart.coord_x, safeStart.coord_y, zoneOf(safeStart));" "the safe start must lie away from the loop")
require_text("${repop}" "bot->SetHomebindToLocation(safeStart, start.zone);\n                        defaultPlayerInfo = nullptr;" "homebind set, homebind branch used")
require_text("${repop}" "if (!moved && defaultPlayerInfo)" "race start at the loop: the homebind fallback")
require_text("${repop}" "!WorldPosition(home).isEnemyHomeZoneFor(bot->GetTeam());" "the homebind fallback is never hostile")
