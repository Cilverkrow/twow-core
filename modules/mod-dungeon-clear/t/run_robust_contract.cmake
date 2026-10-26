# CLI-GRP G1 dry-r3 (25.10.2026, Refs twow-repo#629): run robustness, three server-only DcSettings keys, default 0.
# The rules are pure (DcRunRobustPolicy.h, gtest TestRunRobustPolicy); this pins their wiring as source text.

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
    message(FATAL_ERROR "run robustness: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_count text needle expected description)
  string(REGEX MATCHALL "${needle}" hits "${text}")
  list(LENGTH hits n)
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "run robustness: ${description}: ${n} x '${needle}', want ${expected}")
  endif()
endfunction()

read_dc("src/Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h" registry)
read_dc("conf/mod_dungeon_clear.conf.dist" conf)
read_dc("src/DungeonClearModule.cpp" module)
read_dc("src/DcStrategyGate.cpp" gate)
read_dc("src/Ai/Dungeon/DungeonClear/Action/DcFollowerActions.cpp" follower)
read_dc("src/Ai/Dungeon/DungeonClear/Action/DcAdvanceAction.cpp" advance)

# Keys: registry rows (server-only, default 0) and documented with 0.
require_text("${registry}" "{ \"SuppressDeathLoopEvac\",    DcType::Bool,   0,   0,     1,  false }," "registry SuppressDeathLoopEvac")
require_text("${registry}" "{ \"LootYieldPartyCombatGate\", DcType::Bool,   0,   0,     1,  false }," "registry LootYieldPartyCombatGate")
require_text("${registry}" "{ \"LootYieldReanchorAfterFight\", DcType::Bool, 0,  0,     1,  false }," "registry LootYieldReanchorAfterFight")
require_text("${conf}" "DungeonClear.SuppressDeathLoopEvac = 0" "conf SuppressDeathLoopEvac")
require_text("${conf}" "DungeonClear.LootYieldPartyCombatGate = 0" "conf LootYieldPartyCombatGate")
require_text("${conf}" "DungeonClear.LootYieldReanchorAfterFight = 0" "conf LootYieldReanchorAfterFight")

# Death loop: cleared on the bot's own update (the player hook - the thread that records its deaths), never in the world
# sweep; only alive in a dungeon with an active run, and the key read before the run lookup (pre-review: short-circuit).
require_text("${module}" "        bool const suppress = map && map->IsDungeon() && player->IsAlive() &&
                              DcSettings::GetBool(player, \"SuppressDeathLoopEvac\");
        if (suppress && DcRunRobust::ClearDeathLoop(true, true, true, DcStrategyGate::RunActive(player)))
            ai->ClearDeathLoop();" "death-loop clear in the player update hook")
require_count("${module}" "ai->ClearDeathLoop" 1 "one clear site")
require_text("${gate}" "        return botAI && map && RunActiveFor(botAI, bot, map);" "RunActive = the gate's rule")

# Loot yield: both follower sites with the gate and the fight re-anchor, the tank's advance with the re-anchor only; the
# anchor's second is stamped only when the policy anchored (never per evaluation: a wall-time gap was the pre-review bug).
require_count("${follower}" "DcRunRobust::EvaluateLootYield.lootYield," 2 "follow-tank and hold-at-camp")
require_count("${follower}" "DcSettings::GetBool.bot, .LootYieldPartyCombatGate" 2 "gate at both follower sites")
require_count("${follower}" "DcSettings::GetBool.bot, .LootYieldReanchorAfterFight" 2 "fight re-anchor at both follower sites")
require_count("${follower}" "if .step.anchored." 2 "anchor second stamped on anchoring only (followers)")
require_text("${advance}" "DcRunRobust::EvaluateLootYield(true, false, false, lootYieldStart, fought, now, DC_LOOT_YIELD_TIMEOUT_MS);" "tank: re-anchor only, no combat gate")
require_text("${advance}" "        if (step.anchored)
            approach.lootYieldAnchorSec = uint32(time(nullptr));" "anchor second stamped on anchoring only (tank)")
string(FIND "${follower}${advance}" "LootYieldReanchorGapMs" gap)
if(NOT gap EQUAL -1)
  message(FATAL_ERROR "run robustness: the wall-time gap rule is back (LootYieldReanchorGapMs)")
endif()
