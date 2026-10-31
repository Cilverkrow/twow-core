# twow-repo#601 cand4 lever (ii) (AiPlayerbot.RosterFarMove.LevelGuard, CLI-PATH 31.10.2026, OB-00 order 29.10.; evidence
# far-travel-stuck/levelguard/DESIGN.md): a travel destination with a creature spawn hostile to the bot's side within Radius yd
# whose level_max is >= bot level + Margin is deferred at the target choice, so the main loop takes another destination.
# The rule and the grid are tested in t/far_move_policy_tests.cpp; this contract pins the wiring:
# 1. config: default off, an unknown mode off, margin / radius clamped; ConfigEcho entries sorted; dist conf off.
# 2. the grid is built once at startup, only with the switch on, after the quest travel table.
# 3. PostActiveDeferralOf: LevelGuard after DangerMap and before None, the same gate as the danger map plus the cohort switch
#    and the far-move distance; the answer only through the out parameter (no count, no log in the shared predicate).
# 4. the main loop counts checked / skip / skip_b and writes [FarMoveGuard] (at most LogsPerChoice per choice); the reject line
#    gets level_guard= only when > 0.
# 5. log: [FarMove] guard counts only with the switch; [FarMoveArrive] / [FarMoveDeathTrip] / [FarMoveDeathLate] behind
#    ActivityTrace; the [FarMoveDeath] line and the arrive_deaths count stay as they are.

cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 LevelGuard: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 LevelGuard: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 LevelGuard: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# region(text start end out): from start up to and including the first end after it
function(region text start end out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#601 LevelGuard: region start missing: ${start}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#601 LevelGuard: region end missing after ${start}: ${end}")
  endif()
  string(LENGTH "${end}" end_len)
  math(EXPR b "${b} + ${end_len}")
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("LevelGuardPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/actions/MovementActions.cpp" move)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("QuestSearchPolicy.h" search)

# ---- 1. config ----------------------------------------------------------------------------------------------------
require_text("${config_h}" "uint32 rosterFarMoveLevelGuard = 0;" "member default off")
require_text("${config_cpp}" "rosterFarMoveLevelGuard = config.GetIntDefault(\"AiPlayerbot.RosterFarMove.LevelGuard\", 0);\n    if (rosterFarMoveLevelGuard > 3)\n        rosterFarMoveLevelGuard = 0;" "default 0, mode 3 (cohort A, crossover) accepted, an unknown mode off")
require_text("${config_cpp}" "ai::level_guard::ClampMargin(config.GetIntDefault(\"AiPlayerbot.RosterFarMove.LevelGuard.Margin\", 3))" "margin clamped")
require_text("${config_cpp}" "ai::level_guard::ClampRadius(config.GetFloatDefault(\"AiPlayerbot.RosterFarMove.LevelGuard.Radius\", 50.0f))" "radius clamped")
require_text("${config_cpp}" "            {\"RosterFarMove.ArriveSafe\", Value(rosterFarMoveArriveSafe)},\n            {\"RosterFarMove.LevelGuard\", Value(rosterFarMoveLevelGuard)},\n            {\"RosterFarMove.LevelGuard.Margin\", Value(rosterFarMoveLevelGuardMargin)},\n            {\"RosterFarMove.LevelGuard.Radius\", Value(rosterFarMoveLevelGuardRadius)},\n            {\"Rotation.CastLosGate\"" "ConfigEcho entries, sorted")
require_text("${conf}" "\nAiPlayerbot.RosterFarMove.LevelGuard = 0\nAiPlayerbot.RosterFarMove.LevelGuard.Margin = 3\nAiPlayerbot.RosterFarMove.LevelGuard.Radius = 50\n" "dist conf, off")
require_text("${policy}" "constexpr float DefaultRadius = 50.0f;" "default radius")
require_text("${policy}" "constexpr uint32_t DefaultMargin = 3;" "default margin")
require_text("${policy}" "return spawnLevelMax >= botLevel + margin;" "the rule")
require_text("${policy}" "return (hostileMask & (MaskPlayer | sideMask)) != 0;" "hostile to the bot's side")

# ---- 2. the grid: once, at startup, only with the switch ---------------------------------------------------------------
require_text("${config_cpp}" "    if (sPlayerbotAIConfig.rosterFarMoveLevelGuard)\n        ai::level_guard::BuildGrid();" "built only with the switch")
require_order("${config_cpp}" "sTravelMgr.LoadQuestTravelTable();" "ai::level_guard::BuildGrid();" "after the quest travel table (world data loaded)")
require_text("${choose}" "        std::call_once(gridOnce, []()" "built once")
require_text("${choose}" "                if (data.spawn_flags & SPAWN_FLAG_DISABLED)\n                    continue;" "disabled spawns left out")
require_text("${choose}" "            gridReady.store(true, std::memory_order_release);" "ready after the build")

# ---- 3. the shared predicate ----------------------------------------------------------------------------------------
require_text("${choose}" "    DangerMap,\n    LevelGuard,   // twow-repo#601 cand4 lever (ii)" "deferral reason after DangerMap")
region("${choose}" "PostActiveDeferral PostActiveDeferralOf(" "\n}\n" deferral_fn)
require_text("${deferral_fn}" "danger_map::DangerMap::Hit& dangerHit, ai::level_guard::Hit* guardHit = nullptr)" "out parameter, default none (hub pre-loop)")
require_text("${deferral_fn}" "if (!target->IsForced() && position && ai::cohort::Applies(sPlayerbotAIConfig.rosterFarMoveLevelGuard, bot->GetGUIDLow()) &&\n        !ai::level_guard::ExemptPurpose(uint32(destination->GetPurpose()), uint32(TravelDestinationPurpose::QuestGiver) |\n            uint32(TravelDestinationPurpose::QuestTaker)) &&\n        ai::level_guard::Ready() && UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster() &&\n        position->getMapId() == bot->GetMapId() && position->distance(WorldPosition(bot)) > sPlayerbotAIConfig.reactDistance)" "the gate: not forced, cohort, no giver / taker (OB-00 31.10., #485 / #807), grid ready, roster bot on its own, same map, a far-move trip")
# OB-10 review core#809: the definitions sit outside the anonymous namespace (inside it they would be (anonymous)::ai::level_guard)
string(FIND "${choose}" "\nnamespace\n{\n" anon_at)
string(FIND "${choose}" "\nnamespace ai::level_guard\n{\n" lg_at)
string(FIND "${choose}" "    return takerless;\n}\n\n}\n" anon_end)
if(anon_at EQUAL -1 OR lg_at EQUAL -1 OR anon_end EQUAL -1 OR NOT lg_at GREATER anon_end)
  message(FATAL_ERROR "#601 LevelGuard: the ai::level_guard definitions must come after the anonymous namespace closes")
endif()
require_text("${policy}" "inline bool ExemptPurpose(uint32_t purposeBits, uint32_t exemptMask) { return (purposeBits & exemptMask) != 0; }" "the giver / taker exemption")
require_text("${deferral_fn}" "bot->GetTeam() == ALLIANCE ? ai::level_guard::MaskAlliance : ai::level_guard::MaskHorde" "the bot's side")
require_text("${deferral_fn}" "        if (hit.fired)\n            return PostActiveDeferral::LevelGuard;" "a firing spawn defers")
require_order("${deferral_fn}" "return PostActiveDeferral::DangerMap;" "return PostActiveDeferral::LevelGuard;" "after the danger map")
require_order("${deferral_fn}" "return PostActiveDeferral::LevelGuard;" "return PostActiveDeferral::None;" "before None")
foreach(side_effect "fetch_add" "sLog." "lastRejects" "SharedCounters")
  forbid_text("${deferral_fn}" "${side_effect}" "side effect in the shared predicate (the hub pre-loop must not count or log)")
endforeach()

# ---- 4. the main loop -----------------------------------------------------------------------------------------------
require_text("${choose}" "                PostActiveDeferral const deferral = PostActiveDeferralOf(ai, bot, target, destination, position, hit, &guardHit);\n                if (guardHit.checked)\n                    ai::level_guard::SharedCounters().checked.fetch_add(1, std::memory_order_relaxed);\n                switch (deferral)" "the main loop asks with the out parameter and counts the checks")
require_text("${choose}" "                    case PostActiveDeferral::LevelGuard:\n                    {\n                        ++lastRejects.levelGuard;" "counted as a reject")
require_text("${choose}" "                        if (levelGuardLogged++ < ai::level_guard::LogsPerChoice)" "bounded log per choice")
require_text("${choose}" "[FarMoveGuard] result=skip bot=%u level=%u cohort=%c purpose=%u quest=%u entry=%d map=%u" "the skip line (quest= / entry= for the join)")
require_text("${search}" "(levelGuard ? \" level_guard=\" + std::to_string(levelGuard) : std::string())" "reject line field only when > 0")

# ---- 5. log ---------------------------------------------------------------------------------------------------------
require_text("${move}" "                    if (sPlayerbotAIConfig.rosterFarMoveLevelGuard)\n                    {\n                        ai::level_guard::Counters& g = ai::level_guard::SharedCounters();" "guard counts in [FarMove] only with the switch")
require_text("${move}" "                if (sPlayerbotAIConfig.activityTrace)\n                {\n                    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, \"travel target\");" "[FarMoveArrive] behind ActivityTrace")
require_text("${move}" "[FarMoveArrive] bot=%u level=%u cohort=%c purpose=%u quest=%u entry=%d map=%u tx=%.0f ty=%.0f ax=%.0f ay=%.0f far_dist=%.0f origin=%s" "one line per arrival")
require_order("${move}" "counters.arrived.fetch_add(1, std::memory_order_relaxed);" "[FarMoveArrive]" "at the arrival count")
require_text("${ai_cpp}" "if (sPlayerbotAIConfig.activityTrace && farMove.farMoveArrivedAt && arrivedSeconds <= far_move::ArriveDeathLateSeconds)" "trip line behind ActivityTrace, up to the late window")
require_text("${ai_cpp}" "arrivedSeconds <= far_move::ArriveDeathSeconds ? \"[FarMoveDeathTrip]\" : \"[FarMoveDeathLate]\"" "trip line within 60 s, late line after")
require_text("${ai_cpp}" "sLog.outBasic(\"[FarMoveDeath] bot=%u level=%u map=%u zone=%u x=%.0f y=%.0f after_farmove=1 arrived_s=%u cohort=%c\"," "the [FarMoveDeath] line unchanged")
