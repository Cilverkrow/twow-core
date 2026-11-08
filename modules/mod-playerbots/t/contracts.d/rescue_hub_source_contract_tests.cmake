cmake_policy(VERSION 3.16)
# twow-repo#575 RG-a (OB-00 08.11.2026 decision A with widening; design ws-10/cli-pathfinding/a3-step4-route/
# DESIGN-RG-A-HOMEHUBS-20261108.md; OB-50 anchors SUMS 7c52ff88; CLI-PATH): AiPlayerbot.QuestRescue.HomeHubs (cohort modes,
# default 0). Pins: the pure anchor table, the block before the R1 gate, the R1 gate for next_zone rows, the legacy fallback, the
# lines, config and dist.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "rescue hub: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "rescue hub: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "rescue hub: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "rescue hub: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from `first` to the next `second` after it.
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "rescue hub: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "rescue hub: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("RescueHubPolicy.h" policy)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# 1. Pure policy: the anchor table as data rows (OB-50 innkeepers), no game types, no config, no log.
forbid_text("${policy}" "#include \"playerbot/" "the policy includes a game header")
forbid_text("${policy}" "sPlayerbotAIConfig" "the policy reads config")
forbid_text("${policy}" "sLog" "the policy logs")
forbid_text("${policy}" "Player*" "the policy takes a game type")
require_text("${policy}" "namespace ai::rescue_hub" "own namespace")
require_text("${policy}" "constexpr float OwnHubMinDistance = 100.0f;" "own hub distance")
require_text("${policy}" "constexpr float NextZoneMinDistance = 1000.0f;" "next zone distance = RescueMinDistance")
foreach(row
    "{ 1, 1, 4, Step::OwnHub, OwnHubMinDistance, { 295, 0 } },"
    "{ 3, 1, 4, Step::OwnHub, OwnHubMinDistance, { 1247, 0 } },"
    "{ 7, 1, 4, Step::OwnHub, OwnHubMinDistance, { 1247, 0 } },"
    "{ 4, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6736, 0 } },"
    "{ 4, 5, 10, Step::NextZone, NextZoneMinDistance, { 6737, 0 } },"
    "{ 2, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6928, 0 } },"
    "{ 8, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6928, 0 } },"
    "{ 5, 1, 4, Step::OwnHub, OwnHubMinDistance, { 5688, 0 } },"
    "{ 5, 5, 10, Step::NextZone, NextZoneMinDistance, { 6739, 0 } },"
    "{ 6, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6747, 0 } },"
    "{ 10, 1, 9, Step::OwnHub, OwnHubMinDistance, { 80227, 61831 } },")
  require_text("${policy}" "${row}" "anchor row")
endforeach()
forbid_text("${policy}" "{ 9," "goblin row (its island rules decide)")
require_text("${policy}" "return row && !(row->step == Step::NextZone && startZoneGated);" "R1 gates next_zone rows only")

# 2. Config.
require_text("${config_h}" "uint32 questRescueHomeHubs = 0;" "default off")
require_text("${config_cpp}" "questRescueHomeHubs = ai::rescue_hub::ClampMode(config.GetIntDefault(\"AiPlayerbot.QuestRescue.HomeHubs\", 0));" "mode loader")
require_order("${config_cpp}" "{\"QuestRescue.HomeHubs\", Value(questRescueHomeHubs)}," "{\"QuestRescue.KeepRequestWhenFull\"" "echo, sorted")
require_text("${conf_dist}" "\nAiPlayerbot.QuestRescue.HomeHubs = 0\n" "dist default 0")

# 3. QuestRescueTargets: the table block first (before R1, the goblin island and the legacy race loop); targets found = return;
#    nothing found = fall through to the unchanged legacy code.
between("${mgr}" "std::vector<WorldLocation> RandomPlayerbotMgr::QuestRescueTargets(Player* bot)" "\n}\n" targets_fn)
require_order("${targets_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs, bot->GetGUIDLow())"
  "// twow-repo#601 owner rule R1" "table block before the R1 gate")
require_order("${targets_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs, bot->GetGUIDLow())"
  "UsesGoblinStartRescue(bot->getRace(), level)" "table block before the goblin island rules")
between("${targets_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs" "// twow-repo#601 owner rule R1" block)
require_text("${block}" "ai::zone_keep::StartZoneGated(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevel, level) &&\n            ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevelCohort, bot->GetGUIDLow());" "the bot's R1 gate, as R1 computes it")
require_text("${block}" "rh::Row const* const row = rh::RowFor(bot->getRace(), level);" "row lookup")
require_text("${block}" "if (row && !rh::Usable(row, startZoneGated))" "R1 holds back next_zone")
require_text("${block}" "if (botPos.distance(WorldPosition(spawn)) > row->minDistance)" "per-row distance")
require_text("${block}" "\"[RescueHub] bot=%u race=%u level=%u map=%u zone=%u step=%s anchors=%s targets=%u result=%s\"" "race + anchor line")
require_text("${block}" "\"[RescueHubMin] own_hub=%u next_zone=%u fallback=%u gated=%u\"" "minute line")
require_text("${block}" "        if (!targets.empty())\n            return targets;\n    }" "found = return, else legacy")
forbid_text("${block}" "return {};" "the block never ends the rescue without the legacy path")

message(STATUS "rescue hub source contract: ok")
