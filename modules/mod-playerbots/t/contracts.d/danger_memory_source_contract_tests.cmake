cmake_policy(VERSION 3.16)
# twow-repo#575 A3 step 4 S4-4 (OB-00 07.11.2026; design ws-10/cli-pathfinding/a3-step4-route/DESIGN-STEP4-RESCUE-DANGER-20261107.md
# d54dfd0e + addendum 2 be21560e; CLI-PATH): AiPlayerbot.Danger.Memory (cohort modes, default 0), one read function DangerAt. Pins:
# the pure level rule, QueryAt without the start exclusion (design F4), the real killer read only by QueryAt (the legacy readers
# of the shared map stay byte-equal, both cohorts write it), the death record, the three readers and their gates.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "danger memory: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "danger memory: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "danger memory: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "danger memory: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from `first` to the next `second` after it.
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "danger memory: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "danger memory: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("DangerMemoryPolicy.h" policy)
read_source("DangerMapPolicy.h" dmap)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/MovementActions.cpp" movement)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/values/GrindTargetValue.cpp" grind)

# 1. Pure policy and the level rule.
forbid_text("${policy}" "#include \"playerbot/" "the policy includes a game header")
forbid_text("${policy}" "sPlayerbotAIConfig" "the policy reads config")
forbid_text("${policy}" "sLog" "the policy logs")
forbid_text("${policy}" "Player*" "the policy takes a game type")
require_text("${policy}" "namespace ai::danger_memory" "own namespace")
require_text("${policy}" "constexpr std::uint32_t OwnWindowSeconds = 1800;" "own window 30 min")
require_text("${policy}" "constexpr float OwnRadius = 60.0f;" "own radius 60 yd")
require_text("${policy}" "constexpr std::uint32_t OwnAvoidDeaths = 2;" "two own deaths avoid")
require_text("${policy}" "    if (ownDeaths >= OwnAvoidDeaths || sharedCells > 0)\n        return Level::Avoid;\n    if (ownDeaths > 0)\n        return Level::Caution;\n    return Level::Safe;" "the level rule")

# 2. The shared map: the legacy readers unchanged, QueryAt without the start exclusion, the real killer only in the new reader.
require_text("${dmap}" "inline bool CountsAsDanger(Death const& death, std::uint32_t botLevel, Params const& params)\n{\n    return death.killerLevel >= botLevel + params.levelMargin ||" "legacy rule keeps killerLevel")
between("${dmap}" "inline std::uint32_t DangerousKillerLevel(" "\n}\n" legacy_level)
forbid_text("${legacy_level}" "realKillerLevel" "the legacy cluster rule reads the real killer")
between("${dmap}" "    Hit Query(std::uint32_t map, float fromX" "\n    }\n" legacy_query)
forbid_text("${legacy_query}" "realKillerLevel" "the legacy query reads the real killer")
require_text("${legacy_query}" "if (std::abs(cell.x - start.x) <= 1 && std::abs(cell.y - start.y) <= 1)" "legacy query keeps its start exclusion")
between("${dmap}" "    Hit QueryAt(std::uint32_t map, float x, float y" "\n    }\n" query_at)
forbid_text("${query_at}" "start" "QueryAt has no start exclusion (F4)")
forbid_text("${query_at}" "LineCells" "QueryAt has no line")
require_text("${query_at}" "DangerousRealKillerLevel(it->second, now, botLevel, params)" "QueryAt judges by the real killer")
between("${dmap}" "inline std::uint32_t DangerousRealKillerLevel(" "\n}\n" real_level)
require_text("${real_level}" "!death.realKillerLevel ||\n            death.realKillerLevel < botLevel + params.levelMargin" "no real killer recorded = not counted")
require_text("${real_level}" "victims.size() >= params.minDeaths ? worst : 0" "MinDeaths different bots as the legacy rule")

# 3. Config.
require_text("${config_h}" "uint32 dangerMemory = 0;" "default off")
require_text("${config_cpp}" "dangerMemory = ai::danger_memory::ClampMode(config.GetIntDefault(\"AiPlayerbot.Danger.Memory\", 0));" "mode loader")
require_text("${config_cpp}" "{\"Danger.Memory\", Value(dangerMemory)}," "echo")
require_text("${conf_dist}" "\nAiPlayerbot.Danger.Memory = 0\n" "dist default 0")

# 4. The death: the real killer from the living attackers, recorded only with the key; the per-bot ring for S4-1 or S4-4 bots.
between("${ai_cpp}" "void PlayerbotAI::OnDeath()" "\nvoid PlayerbotAI::" on_death)
require_text("${on_death}" "realKillerLevel = std::max<uint32>(realKillerLevel, unit->GetLevel());" "highest living attacker")
require_text("${on_death}" "if (!sPlayerbotAIConfig.dangerMemory)\n                    realKillerLevel = 0;" "nothing recorded with the key 0")
require_text("${on_death}" "RecordDangerMapDeath(bot, AI_VALUE(Unit*, \"current target\"), deathAttackers, realKillerLevel);" "record passes it")
require_text("${on_death}" "ai::cohort::Applies(sPlayerbotAIConfig.dangerMemory, bot->GetGUIDLow()))" "own deaths recorded for S4-4 bots")
between("${ai_cpp}" "void RecordDangerMapDeath(" "\n}\n" record)
require_order("${record}" "if (sPlayerbotAIConfig.dangerMemory)" "death.realKillerLevel =" "real killer field only with the key")
require_text("${record}" "death.killerLevel = environment ? danger_map::EnvironmentKillerLevel : uint8(std::min<uint32>(255, killer->GetLevel()));" "legacy killer unchanged")

# 5. DangerAt: both layers, the level rule, the lines.
require_text("${ai_h}" "ai::danger_memory::Level DangerAt(uint32 mapId, float x, float y, ai::danger_memory::Query query," "declaration")
between("${ai_cpp}" "ai::danger_memory::Level PlayerbotAI::DangerAt(" "\n}\n" danger_at)
require_text("${danger_at}" "dm::OwnDeathsNear(corpseFirstDeaths, now, mapId, x, y)" "per-bot layer = the S4-1 ring")
require_text("${danger_at}" "danger_map::Instance().QueryAt(mapId, x, y, bot->GetLevel(), now, params).cells" "shared layer = QueryAt")
require_text("${danger_at}" "dm::Level const level = dm::Combine(own, shared);" "the level rule")
require_text("${danger_at}" "[Danger] bot=%u cohort=%c level_bot=%u q=%s map=%u x=%.0f y=%.0f level=%s own=%u shared=%u" "danger line")
require_text("${danger_at}" "[DangerMin] target_avoid=%u arrive_avoid=%u grind_avoid=%u caution=%u own_avoid=%u shared_avoid=%u real_killer_recorded=%u" "minute line")
require_text("${danger_at}" "counters.LineAllowed()" "line budget")

# 6. Readers.
# (ii) arrival point: DangerAt for the switch's bots instead of the legacy from == to query (F4); legacy for everyone else.
require_order("${movement}" "if (pointAi && ai::cohort::Applies(sPlayerbotAIConfig.dangerMemory, bot->GetGUIDLow()) && !pointAi->HasRealPlayerMaster())"
  "    else if (sPlayerbotAIConfig.dangerMapEnabled)\n    {\n        // re-deaths at camps (DangerMap.CountAddDeaths)" "arrival: switch branch before the legacy branch")
require_text("${movement}" "pointAi->DangerAt(point.getMapId(), point.getX(), point.getY(), ai::danger_memory::Query::Arrive) == ai::danger_memory::Level::Avoid)\n            return false;" "arrival: avoid = no point")
# (i) travel target: after the legacy deferral, same deferral.
require_order("${choose}" "        if (dangerHit.cells)\n            return PostActiveDeferral::DangerMap;" "ai::danger_memory::Query::Target" "target: legacy query first")
require_text("${choose}" "if (!target->IsForced() && position && ai::cohort::Applies(sPlayerbotAIConfig.dangerMemory, bot->GetGUIDLow()) &&\n        UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster() && position->getMapId() == bot->GetMapId() &&" "target: population")
require_text("${choose}" "ai::danger_memory::Query::Target) == ai::danger_memory::Level::Avoid)\n        return PostActiveDeferral::DangerMap;" "target: avoid = deferral")
# (iii) grind: roster bot on its own, after GrindAvoid, cached per pass.
require_text("${grind}" "bool const dangerMemoryGrind = rosterOnItsOwn && ai::cohort::Applies(sPlayerbotAIConfig.dangerMemory, bot->GetGUIDLow());" "grind: population")
require_order("${grind}" "logGrind(unit, \"ignored (killed this bot repeatedly).\");" "logGrind(unit, \"ignored (danger memory: avoided place).\");" "grind: after GrindAvoid")
require_text("${grind}" "ai::danger_memory::Query::Grind, &dangerCells)" "grind: per-pass cell cache")

message(STATUS "danger memory source contract: ok")
