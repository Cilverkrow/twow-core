cmake_policy(VERSION 3.16)
if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#575 F9 / F-b V2 (owner 29.10.2026, Ä17 "Zwischen-Update", "Bizzlo-Platz, ~140 m"): a level-limited graveyard
# from custom_graveyards (Blackstone Island, zone 5536, up to L10 at the Bizzlo plaza; graveyard 149 from L11).
# 1. Policy: the rule (level 0 = none, level <= max_level, map + zone, area 0 / = zone = whole zone), synthetic ids.
# 2. ObjectMgr: loader only for Graveyard.Custom.Zones, the custom lookup before the zone's normal graveyards, loaded once
#    after the graveyard zones (pointers stay valid), facing for the synthetic id.
# 3. Callers that pass the level: repop, BOTH spirit-healer lookups (else a teleport back to 149), the map stub, the bot
#    graveyard value. Battlegrounds stay without a level.
# 4. Config default on, allowlist 5536; migration: LF, guarded inserts, end-state CHECK, rollback.
set(src "${TW_CORE_ROOT}/src/game")

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#575 F-b V2 ${label}: missing ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#575 F-b V2 ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected label)
  string(REPLACE "${needle}" "\n@@HIT@@\n" marked "${text}")
  string(REGEX MATCHALL "@@HIT@@" hits "${marked}")
  list(LENGTH hits n)
  if (NOT n EQUAL expected)
    message(FATAL_ERROR "#575 F-b V2 ${label}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

file(READ "${src}/CustomGraveyardPolicy.h" policy)
file(READ "${src}/ObjectMgr.h" mgr_h)
file(READ "${src}/ObjectMgr.cpp" mgr)
file(READ "${src}/World.h" world_h)
file(READ "${src}/World.cpp" world)
file(READ "${src}/Objects/Player.cpp" player)
file(READ "${src}/Handlers/NPCHandler.cpp" npc)
file(READ "${src}/Maps/Map.h" map_h)
file(READ "${src}/Maps/Map.cpp" map_cpp)
file(READ "${TW_CORE_ROOT}/modules/mod-playerbots/src/playerbot/strategy/values/DeadValues.cpp" dead)
file(READ "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in" conf)

# 1. policy
require_text("${policy}" "return level != 0 && level <= rowMaxLevel && rowMap == map && rowZone == zone && AreaFits(rowZone, rowArea, area);" "the rule")
require_text("${policy}" "return rowArea == 0 || rowArea == rowZone || rowArea == area;" "the area rule")
require_text("${policy}" "constexpr uint32_t IdBase = 100000;" "synthetic id base above the DBC")

# 2. ObjectMgr
require_text("${mgr}" "#include \"CustomGraveyardPolicy.h\"" "policy include")
require_text("${mgr_h}" "WorldSafeLocsEntry const *GetClosestGraveYard(float x, float y, float z, uint32 MapId, Team team, uint32 level = 0);" "level parameter, default 0")
require_text("${mgr}" "if (!sWorld.getConfig(CONFIG_BOOL_GRAVEYARD_CUSTOM_ENABLE))" "master switch")
require_text("${mgr}" "custom_graveyard::ParseZones(zoneText)" "zone allowlist")
require_text("${mgr}" "        if (!zones.count(zoneId))\n            continue;" "only allowlisted zones")
require_text("${mgr}" "m_worldSafeLocsFacingMap[data.loc.ID] = facing;" "facing for the synthetic id")
require_text("${mgr}" "if (!custom_graveyard::Applies(data.rowMap, data.zoneId, data.areaId, data.maxLevel, MapId, zoneId, areaId, level))" "lookup uses the rule")
require_text("${mgr}" "if (!custom_graveyard::TeamFits(data.horde, uint32(team)))" "lookup uses the team rule")
require_order("${mgr}" "if (WorldSafeLocsEntry const* custom = GetCustomGraveYard(x, y, z, MapId, zoneId, team, level))" "GraveYardMapBounds bounds = m_GraveYardMap.equal_range(zoneId);" "custom before the zone's normal graveyards")
require_order("${world}" "sObjectMgr.LoadGraveyardZones();" "sObjectMgr.LoadCustomGraveyards();" "loaded after the graveyard zones")
require_order("${world}" "sObjectMgr.LoadWorldSafeLocsFacing();" "sObjectMgr.LoadCustomGraveyards();" "after the facings (the loader adds to them)")
require_count("${world}" "sObjectMgr.LoadCustomGraveyards();" 1 "loaded once (no reload: pointers stay valid)")
require_text("${world_h}" "CONFIG_BOOL_GRAVEYARD_CUSTOM_ENABLE," "config enum")
require_text("${world}" "setConfig(CONFIG_BOOL_GRAVEYARD_CUSTOM_ENABLE, \"Graveyard.Custom.Enable\", true);" "default on")

# 3. callers
require_text("${player}" "ClosestGrave = sObjectMgr.GetClosestGraveYard(GetPositionX(), GetPositionY(), GetPositionZ(), GetMapId(), GetTeam(), GetLevel());" "repop passes the level")
require_count("${npc}" "_player->GetTeam(), _player->GetLevel());" 2 "both spirit-healer lookups pass the level")
require_text("${map_h}" "WorldSafeLocsEntry const* GetClosestGraveYard(float x, float y, float z, uint32 MapId, Team team, uint32 level = 0) const;" "map stub level")
require_text("${map_cpp}" "return sObjectMgr.GetClosestGraveYard(x, y, z, MapId, team, level);" "map stub forwards the level")
require_text("${dead}" "        bot->GetTeam(),\n        bot->GetLevel()" "bot graveyard value passes the level")

# 4. config + migration
require_text("${conf}" "Graveyard.Custom.Enable = 1" "dist conf switch")
require_text("${conf}" "Graveyard.Custom.Zones = \"5536\"" "dist conf allowlist")
set(mig "${TW_CORE_ROOT}/sql/database_updates/20261029200100_world.sql")
if (NOT EXISTS "${mig}")
  message(FATAL_ERROR "#575 F-b V2: missing ${mig}")
endif()
file(READ "${mig}" sql)
file(READ "${mig}" hex HEX)
if (hex MATCHES "^(..)*0d")
  message(FATAL_ERROR "#575 F-b V2: CRLF in the migration")
endif()
require_text("${sql}" "\"Setzte bitte v1 und v2 um.\"" "owner quote")
require_text("${sql}" "--   DELETE FROM `custom_graveyards` WHERE `id` = 51 AND `zone_id` = 5536;" "rollback row")
require_text("${sql}" "--   DELETE FROM `creature` WHERE `guid` = 2990011 AND `id` = 6491;" "rollback healer")
require_text("${sql}" "SELECT 51, 'Blackstone Island - Bizzlo plaza', 1, 5536, 5536, 10," "row: map 1, zone 5536, whole zone, L10")
require_text("${sql}" " WHERE NOT EXISTS (SELECT 1 FROM `custom_graveyards` WHERE `zone_id` = 5536)" "row guard")
require_text("${sql}" " WHERE NOT EXISTS (SELECT 1 FROM `creature` WHERE `guid` = 2990011)" "healer guid guard")
require_text("${sql}" "CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_gy149v2` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));" "end-state check")
string(REGEX REPLACE "(^|\n)--[^\n]*" "\\1" body "${sql}")
string(TOUPPER "${body}" upper)
foreach (forbidden "DELETE " "DROP TABLE" "TRUNCATE" "REPLACE INTO" "ALTER TABLE" "UPDATE ")
  string(FIND "${upper}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "#575 F-b V2: forbidden statement ${forbidden}")
  endif()
endforeach()

message(STATUS "custom_graveyard_575_contract: ok")
