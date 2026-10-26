# CLI-GRP g1n (26.10.2026, Refs twow-repo#629): DungeonClear.StayDeadInRun, default 0. A dead member of an active run
# (or of a test party) stays a corpse in the dungeon instead of releasing out of the instance. The rule is pure
# (DcRunRobust::StayDead, gtest TestRunRobustPolicy); this pins its wiring as source text: the predicate goes through
# the playerbots release veto (playerbot/ReleaseVeto.h), never through a DC override of the stock "auto release".

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_dc path out_var)
  file(READ "${DC_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "stay dead: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "stay dead: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_dc("src/Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h" registry)
read_dc("conf/mod_dungeon_clear.conf.dist" conf)
read_dc("src/DungeonClearModule.cpp" module)
read_dc("src/Ai/Dungeon/DungeonClear/DungeonClearActionContext.h" context)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DcRunRobustPolicy.h" policy)

# Key: registry row (server-only, default 0), documented with 0.
require_text("${registry}" "{ \"StayDeadInRun\",            DcType::Bool,   0,   0,     1,  false }," "registry StayDeadInRun")
require_text("${conf}" "DungeonClear.StayDeadInRun = 0" "conf StayDeadInRun")
require_text("${registry}" "{ \"StayDeadMaxSecs\",          DcType::UInt, 600,  60,  3600,  false }," "registry StayDeadMaxSecs")
require_text("${conf}" "DungeonClear.StayDeadMaxSecs = 600" "conf StayDeadMaxSecs")

# Predicate: map and dungeon first, the key only in a dungeon, then the pure rule with the run and the test party.
require_order("${module}" "bool StayDeadInRunVeto(Player* bot, std::int64_t deadSeconds)" "        if (!map || !map->IsDungeon() || !DcSettings::GetBool(bot, \"StayDeadInRun\"))
            return false;" "predicate guards")
# Bounded (pre-review 26.10.): a living rezzer on this map and the wait cap, with the seconds playerbots passes in.
require_order("${module}" "        return DcRunRobust::StayDead(true, true, DcStrategyGate::RunActive(bot),
                                     sRandomPlayerbotMgr.IsExternallyManaged(bot->GetGUIDLow())," "                                     DcRezRecovery::LivingRezzerOnMap(bot), deadSeconds,
                                     DcSettings::GetUInt(bot, \"StayDeadMaxSecs\"));" "predicate rule with its bounds")
require_text("${policy}" "    return on && inDungeon && (runActive || testParty) && rezzerAlive && deadSeconds >= 0 && deadSeconds < maxSeconds;" "pure bounded rule")

# Registration: once, in the module's AddSC, through the playerbots registry.
require_order("${module}" "void AddSC_dungeon_clear_module()" "    ai::release_veto::Register(&StayDeadInRunVeto);" "registered in AddSC")

# The stock-name override stays disabled (the 22.08. map-pool crash path).
require_text("${context}" "        // creators[\"auto release\"] = &DungeonClearActionContext::auto_release;" "auto release override disabled")
string(FIND "${context}" "\n        creators[\"auto release\"]" live_override)
if(NOT live_override EQUAL -1)
  message(FATAL_ERROR "stay dead: the stock \"auto release\" override is live again; use the release veto")
endif()
