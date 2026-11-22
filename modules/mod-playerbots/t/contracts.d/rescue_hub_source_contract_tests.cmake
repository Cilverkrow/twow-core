cmake_policy(VERSION 3.16)
# twow-repo#575 RG-a (OB-00 08.11.2026 decision A with widening; design ws-10/cli-pathfinding/a3-step4-route/
# DESIGN-RG-A-HOMEHUBS-20261108.md; OB-50 anchors SUMS 7c52ff88; CLI-PATH): AiPlayerbot.QuestRescue.HomeHubs (cohort modes,
# default 1 since the SZM adoption 10.11.2026, 0 = rollback). Pins: the pure anchor table, the block before the R1 gate, the R1 gate for next_zone rows, the legacy fallback, the
# lines, config and dist.
# twow-repo#703 item 2 (design szf/DESIGN-703-2-VALLEY-L5-20261111.md d9a6539c, OB-10 review 49953b8c): the sub-key
# AiPlayerbot.QuestRescue.HomeHubs.L5OwnHub (cohort modes, default 0): the L5-10 own-hub rows only behind the flag, the at_hub rule
# only for those rows (spawns, all near: no target, no legacy), the legacy fallback only without any spawn.
# twow-repo#575 RescueHub LevelFloor (OB-00 19.11.2026; census rescue-hub-floor/CENSUS-RESCUEHUB-DAUERB-20261119.md 43a83b6c): the sub-key
# AiPlayerbot.QuestRescue.HomeHubs.LevelFloor (cohort modes, default 0): undead L5-9 at the own hub Brill (at_hub rule), the Sepulcher
# only from L10, searched before the main table and only behind the flag; night elves unchanged.

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
require_text("${config_h}" "uint32 questRescueHomeHubs = 1;" "default 1 (SZM adoption)")
require_text("${config_cpp}" "questRescueHomeHubs = ai::rescue_hub::ClampMode(config.GetIntDefault(\"AiPlayerbot.QuestRescue.HomeHubs\", 1));" "mode loader, default 1")
require_order("${config_cpp}" "{\"QuestRescue.HomeHubs\", Value(questRescueHomeHubs)}," "{\"QuestRescue.KeepRequestWhenFull\"" "echo, sorted")
require_text("${conf_dist}" "\nAiPlayerbot.QuestRescue.HomeHubs = 1\n" "dist default 1")
require_text("${conf_dist}" "# Cohort mode: 0 = off (rollback), 1 = all bots (default), 2 = cohort B (odd guid), 3 = cohort A (even guid); other values act like 0.\nAiPlayerbot.QuestRescue.HomeHubs = 1\n" "dist mode text: 0 = rollback, 1 = default")
require_text("${conf_dist}" "HomeHubs has no A/B of its own yet" "dist states the adoption came with SZM, not from its own A/B")

# 3. QuestRescueTargets: the table block first (before R1, the goblin island and the legacy race loop); targets found = return;
#    nothing found = fall through to the unchanged legacy code.
between("${mgr}" "std::vector<WorldLocation> RandomPlayerbotMgr::QuestRescueTargets(Player* bot)" "\n}\n" targets_fn)
require_order("${targets_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs, bot->GetGUIDLow())"
  "// twow-repo#601 owner rule R1" "table block before the R1 gate")
require_order("${targets_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs, bot->GetGUIDLow())"
  "UsesGoblinStartRescue(bot->getRace(), level)" "table block before the goblin island rules")
between("${targets_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs" "// twow-repo#601 owner rule R1" block)
require_text("${block}" "ai::zone_keep::StartZoneGated(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevel, level) &&\n            ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevelCohort, bot->GetGUIDLow());" "the bot's R1 gate, as R1 computes it")
require_text("${block}" "bool const l5OwnHub = ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubsL5OwnHub, bot->GetGUIDLow());\n        bool const levelFloor = ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubsLevelFloor, bot->GetGUIDLow());\n        rh::Row const* const row = rh::RowFor(bot->getRace(), level, sPlayerbotAIConfig.customRaceStartZones, l5OwnHub, levelFloor);" "row lookup with the custom starts switch and the L5 and LevelFloor flags of the bot's cohort")
# SZM adoption review (10.11.2026): the start-zone rules of the rescue act only on roster bots without a real player master (as
# StartZoneHolds), and the high-elf row needs CustomRaceStartZones.
require_order("${targets_fn}" "bool const startZoneRules = IsPersistentRosterMember(bot->GetGUIDLow()) && !(rescueAi && rescueAi->HasRealPlayerMaster());"
  "if (startZoneRules && ai::cohort::Applies(sPlayerbotAIConfig.questRescueHomeHubs, bot->GetGUIDLow()))" "HomeHubs only for roster bots on their own")
require_text("${policy}" "inline Row const* RowFor(std::uint32_t race, std::uint32_t level, bool customStarts, bool l5OwnHub = false, bool levelFloor = false)\n{\n    if (race == HighElfRace && !customStarts)\n        return nullptr;" "high-elf row only with custom starts (before every table)")
forbid_text("${targets_fn}" "rh::RowFor(bot->getRace(), level);" "a row lookup without the custom starts switch")
require_text("${block}" "if (row && !rh::Usable(row, startZoneGated))" "R1 holds back next_zone")
require_text("${block}" "if (botPos.distance(WorldPosition(spawn)) > row->minDistance)" "per-row distance")
require_text("${block}" "\"[RescueHub] bot=%u race=%u level=%u map=%u zone=%u step=%s anchors=%s targets=%u result=%s\"" "race + anchor line")
require_text("${block}" "\"[RescueHubMin] own_hub=%u next_zone=%u fallback=%u gated=%u at_hub=%u\"" "minute line (at_hub appended at the end)")
# twow-repo#575 R3 (logging only): a fallback gets its own line with the cause (no anchor spawn / all spawns within the minimum distance),
# after the [RescueHub] line; the [RescueHub] line itself is unchanged (parsers key on it).
require_order("${block}" "uint32(targets.size()), rh::OutcomeName(outcome));"
  "if (outcome == rh::Outcome::Fallback)\n                sLog.outBasic(\"[RescueHubFallback] bot=%u race=%u level=%u map=%u zone=%u step=%s anchors=%s spawns=%u min_dist=%.0f reason=%s\"," "fallback cause line after the RescueHub line")
require_text("${block}" "anchors.c_str(), uint32(anchorSpawns), row->minDistance, anchorSpawns ? \"near_only\" : \"no_spawn\");" "near_only = spawns but none far; no_spawn = none")
require_text("${block}" "        if (!targets.empty() || atHub)\n            return targets;\n    }" "found or at_hub = return, else legacy")
forbid_text("${block}" "return {};" "the block never ends the rescue without the legacy path")

# 4. twow-repo#703 item 2: HomeHubs.L5OwnHub.
#    a) The L5-10 rows: a separate table, searched only with the flag, after the main table (night elf / undead keep next_zone).
foreach(row
    "{ 1, 5, 10, Step::OwnHub, OwnHubMinDistance, { 295, 0 } },"
    "{ 3, 5, 10, Step::OwnHub, OwnHubMinDistance, { 1247, 0 } },"
    "{ 7, 5, 10, Step::OwnHub, OwnHubMinDistance, { 1247, 0 } },"
    "{ 2, 5, 10, Step::OwnHub, OwnHubMinDistance, { 6928, 0 } },"
    "{ 8, 5, 10, Step::OwnHub, OwnHubMinDistance, { 6928, 0 } },"
    "{ 6, 5, 10, Step::OwnHub, OwnHubMinDistance, { 6747, 0 } },")
  require_text("${policy}" "${row}" "L5 own-hub row")
endforeach()
require_text("${policy}" "constexpr std::array<Row, 6> L5OwnHubTable{{" "the L5 rows are a table of their own (Table keeps its 11 rows)")
require_text("${policy}" "constexpr std::array<Row, 11> Table{{" "the main table is unchanged in size")
between("${policy}" "inline Row const* RowFor(" "\n}\n" rowfor)
require_order("${rowfor}" "for (Row const& row : Table)" "if (l5OwnHub)\n        for (Row const& row : L5OwnHubTable)" "main table first, the L5 table only behind the flag")
string(REGEX MATCHALL "L5OwnHubTable" l5_refs "${rowfor}")
list(LENGTH l5_refs l5_ref_count)
if(NOT l5_ref_count EQUAL 1)
  message(FATAL_ERROR "rescue hub: RowFor reads the L5 table ${l5_ref_count} times (expected once, behind the flag)")
endif()
#    b) The outcome: hub with a far spawn; at_hub only for an L5 row with spawns; fallback (legacy) otherwise.
between("${policy}" "inline Outcome Decide(" "\n}\n" decide)
require_text("${decide}" "    if (farSpawns > 0)\n        return Outcome::Hub;\n    if (spawns > 0 && IsL5OwnHubRow(row))\n        return Outcome::AtHub;\n    return Outcome::Fallback;" "at_hub only for an L5 row with spawns, all near")
require_text("${policy}" "case Outcome::Hub: return \"hub\";" "hub name as in today's line")
require_text("${policy}" "case Outcome::AtHub: return \"at_hub\";" "at_hub name")
require_text("${policy}" "case Outcome::Fallback: return \"fallback\";" "fallback name as in today's line")
#    c) The block: every spawn counted, the outcome from Decide, the counters, the line, the return.
require_text("${block}" "                for (WorldLocation const& spawn : cached->second)\n                {\n                    ++anchorSpawns;\n                    if (botPos.distance(WorldPosition(spawn)) > row->minDistance)\n                        targets.push_back(spawn);\n                }" "every anchor spawn counted, far ones are targets")
require_text("${block}" "rh::Outcome const outcome = rh::Decide(row, anchorSpawns, targets.size());" "the outcome from the pure rule")
require_text("${block}" "else if (outcome == rh::Outcome::AtHub)\n                counters.atHub.fetch_add(1, std::memory_order_relaxed);" "at_hub counted")
require_text("${block}" "atHub = outcome == rh::Outcome::AtHub;" "at_hub flag only from the outcome")
require_text("${block}" "uint32(targets.size()), rh::OutcomeName(outcome));" "the line names the outcome")
require_text("${block}" "counters.atHub.exchange(0, std::memory_order_relaxed)" "at_hub minute count resets")
string(REGEX MATCHALL "atHub = " athub_sets "${block}")
list(LENGTH athub_sets athub_set_count)
if(NOT athub_set_count EQUAL 2)
  message(FATAL_ERROR "rescue hub: atHub is set ${athub_set_count} times in the block (expected twice: the false start and the outcome)")
endif()
require_text("${block}" "bool atHub = false;" "at_hub starts false (flag off: never set)")
forbid_text("${block}" "l5OwnHub = true" "the L5 flag forced on")
forbid_text("${targets_fn}" "RowFor(bot->getRace(), level, sPlayerbotAIConfig.customRaceStartZones, true)" "the L5 rows without the key")
#    d) Config: default 0, cohort modes, an unknown mode is off and logged; echo sorted; dist 0.
require_text("${config_h}" "uint32 questRescueHomeHubsL5OwnHub = 0;" "L5 key default 0")
require_text("${config_cpp}" "questRescueHomeHubsL5OwnHub = config.GetIntDefault(\"AiPlayerbot.QuestRescue.HomeHubs.L5OwnHub\", 0);\n    if (questRescueHomeHubsL5OwnHub > 3)\n    {\n        sLog.outError(\"[RescueHub] config key=HomeHubs.L5OwnHub value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", questRescueHomeHubsL5OwnHub);\n        questRescueHomeHubsL5OwnHub = 0;\n    }" "L5 key loader: cohort modes, other = off, logged")
require_order("${config_cpp}" "{\"QuestRescue.HomeHubs\", Value(questRescueHomeHubs)},\n            {\"QuestRescue.HomeHubs.L5OwnHub\", Value(questRescueHomeHubsL5OwnHub)},\n" "{\"QuestRescue.KeepRequestWhenFull\"" "echo, sorted")
require_text("${conf_dist}" "# Cohort mode: 0 = off (default, today's table), 1 = all bots, 2 = cohort B (odd guid), 3 = cohort A (even guid); other values act like 0.\nAiPlayerbot.QuestRescue.HomeHubs.L5OwnHub = 0\n" "dist default 0 with the mode text")

# 5. twow-repo#575 RescueHub LevelFloor.
#    a) The rows: undead only, L5-9 own hub Brill, L10 the Sepulcher; the main table keeps its undead / night-elf next_zone rows.
require_text("${policy}" "constexpr std::array<Row, 2> LevelFloorTable{{\n    { 5, 5, 9, Step::OwnHub, OwnHubMinDistance, { 5688, 0 } },        // undead L5-9: Innkeeper Renee 28474, Brill\n    { 5, 10, 10, Step::NextZone, NextZoneMinDistance, { 6739, 0 } },  // undead L10: Innkeeper Bates 17865, The Sepulcher\n}};" "the LevelFloor rows (undead only)")
between("${policy}" "constexpr std::array<Row, 2> LevelFloorTable{{" "}};" floor_rows)
forbid_text("${floor_rows}" "{ 4," "a night-elf LevelFloor row (OB-00 19.11.: night elves keep Auberdine)")
#    b) RowFor: the flag is a defaulted last parameter; the LevelFloor table once, behind the flag, before the main table.
require_text("${policy}" "inline Row const* RowFor(std::uint32_t race, std::uint32_t level, bool customStarts, bool l5OwnHub = false, bool levelFloor = false)\n{\n    if (race == HighElfRace && !customStarts)\n        return nullptr;\n    if (levelFloor)\n        for (Row const& row : LevelFloorTable)" "LevelFloor table behind the flag, first")
require_order("${rowfor}" "if (levelFloor)\n        for (Row const& row : LevelFloorTable)" "for (Row const& row : Table)" "LevelFloor before the main table")
string(REGEX MATCHALL "LevelFloorTable" floor_refs "${rowfor}")
list(LENGTH floor_refs floor_ref_count)
if(NOT floor_ref_count EQUAL 1)
  message(FATAL_ERROR "rescue hub: RowFor reads the LevelFloor table ${floor_ref_count} times (expected once, behind the flag)")
endif()
#    c) at_hub: the L5-9 own-hub row of the LevelFloor table only (the L10 next_zone row keeps today's fallback).
require_text("${policy}" "    for (Row const& r : LevelFloorTable)\n        if (row == &r && r.step == Step::OwnHub)\n            return true;" "at_hub for the LevelFloor own-hub row only")
forbid_text("${block}" "levelFloor = true" "the LevelFloor flag forced on")
forbid_text("${targets_fn}" "l5OwnHub, true)" "the LevelFloor rows without the key")
#    d) Config: default 0, cohort modes, an unknown mode is off and logged; echo sorted; dist 0.
require_text("${config_h}" "uint32 questRescueHomeHubsLevelFloor = 0;" "LevelFloor key default 0")
require_text("${config_cpp}" "questRescueHomeHubsLevelFloor = config.GetIntDefault(\"AiPlayerbot.QuestRescue.HomeHubs.LevelFloor\", 0);\n    if (questRescueHomeHubsLevelFloor > 3)\n    {\n        sLog.outError(\"[RescueHub] config key=HomeHubs.LevelFloor value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", questRescueHomeHubsLevelFloor);\n        questRescueHomeHubsLevelFloor = 0;\n    }" "LevelFloor key loader: cohort modes, other = off, logged")
require_order("${config_cpp}" "{\"QuestRescue.HomeHubs.L5OwnHub\", Value(questRescueHomeHubsL5OwnHub)},\n            {\"QuestRescue.HomeHubs.LevelFloor\", Value(questRescueHomeHubsLevelFloor)},\n" "{\"QuestRescue.KeepRequestWhenFull\"" "echo, sorted")
require_text("${conf_dist}" "# Cohort mode: 0 = off (default, today's table), 1 = all bots, 2 = cohort B (odd guid), 3 = cohort A (even guid); other values act like 0.\nAiPlayerbot.QuestRescue.HomeHubs.LevelFloor = 0\n" "LevelFloor dist default 0 with the mode text")

message(STATUS "rescue hub source contract: ok")
