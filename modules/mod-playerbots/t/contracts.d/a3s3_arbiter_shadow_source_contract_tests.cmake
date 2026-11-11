cmake_policy(VERSION 3.16)
# A3 step 3, behaviour pair 1 (ADR-0041; OB-00 07.11.2026 E1-E4 a and build rules 1-4; owner OK 08.11.2026): the arbiter shadow,
# AiPlayerbot.Supervisor.Arbiter (0 off = default, 1 shadow, 2 enforce = A3 step 3 T02-a, a3s3_arbiter_enforce_source_contract_tests.cmake; other = 0 + error line). Design
# ws-00/projekt-durchleuchtung/A3-S3-ARBITER-SCHATTEN-ENTWURF.md v1.2, section 6.1. The shadow only watches. This contract pins:
#   - the hook: one observer call in the module, the last statement of TravelTarget::SetStatus (after the band clamp), one line
#     without a block, only for the live target;
#   - the live target: live_ written only by MarkLive, MarkLive called only in the TravelTargetValue constructor;
#   - no write from the shadow: SupervisorShadow.h/.cpp name no writer, no value write, no config read; the untagged branch of
#     the observer counts and returns before it touches the owner's supervisor (build rule 3); one file-local mutex guards the
#     budget, the minute sums and the bundles (build rule 1);
#   - the ratchet, armed: direct travel-target writer calls in the module stay <= the count of this pair's base (97);
#   - the scopes: every entry function of the writer inventory carries its WriterScope before its first writer call;
#     setNewTarget falls back to Brain only on an empty stack; the setNewTarget call sites stay the known ones;
#   - the thread tags: the bot's UpdateAI, the world hook and the console handler (build rule 2);
#   - the switch: default 0, clamp > 2 -> 0 with an error line, documented 0, echoed; read only at the gate, parent switch first;
#   - "random bot update" stays dormant (no setter) and nothing replaces the live travel target pointer.
# Pure rules: t/supervisor_policy_tests.cpp (TestArbiter). Every check_* returns "" when it holds, else the reason; the negative
# probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s3 arbiter: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s3 arbiter: PB_MODULE_DIR is required")
endif()

set(TAG "a3s3 arbiter shadow")
set(RATCHET_BASE 99)   # (SetTarget|SetStatus|SetNullTravelTarget)( in the module: 97 at release/9.x 55cd53f8, 98 with A3 step 3 T02-a (the probe in SupervisorEnforce.cpp: r1 on the live target and the local copy; pair a3s3_arbiter_enforce_source_contract_tests.cmake), 99 with twow-repo#703 item 4 (TravelTarget::MarkUnreachableCamp ends the target, Execution scope in SCOPES; Rotation.UnreachableGiveUp, default 0)

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("SupervisorShadow.h" shadow_h)
read_source("SupervisorShadow.cpp" shadow_cpp)
read_source("SupervisorPolicy.h" policy)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_cpp)
read_source("strategy/values/TargetValue.h" target_value_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotScripts.cpp" scripts_cpp)
read_source("RandomPlayerbotMgr.cpp" rnd_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("QuestBand.cpp" band_cpp)
read_source("QuestGroupWorld.cpp" group_world_cpp)
read_source("strategy/actions/MoveToTravelTargetAction.cpp" move_cpp)
read_source("strategy/values/TravelValues.cpp" travel_values_cpp)
read_source("strategy/actions/ReleaseSpiritAction.h" release_h)
read_source("strategy/actions/ZoneEscapeActions.cpp" escape_cpp)
read_source("strategy/triggers/StationCraftTrigger.cpp" station_cpp)
read_source("strategy/actions/GuildCreateActions.cpp" guild_cpp)
read_source("strategy/actions/FollowActions.cpp" follow_cpp)
read_source("strategy/actions/WorldBuffTravelActions.cpp" worldbuff_cpp)
read_source("strategy/actions/GoAction.cpp" go_cpp)
read_source("strategy/actions/DebugAction.cpp" debug_cpp)
read_source("strategy/tests/CommandSetup.cpp" setup_cpp)
file(READ "${PB_MODULE_DIR}/t/supervisor_policy_tests.cpp" unit_test)
string(REPLACE "\r" "" unit_test "${unit_test}")

# Review 08.11.: a file that names a writer scope or a thread tag must include the shadow header (or Supervisor.h, which
# includes it); a scope line alone compiles nowhere else.
function(scope_file_ok text out)
  set(${out} TRUE PARENT_SCOPE)
  string(FIND "${text}" "ai::supervisor::WriterScope" a)
  string(FIND "${text}" "ai::supervisor::ThreadTag" b)
  if(a EQUAL -1 AND b EQUAL -1)
    return()
  endif()
  string(FIND "${text}" "#include \"playerbot/SupervisorShadow.h\"" c)
  string(FIND "${text}" "#include \"playerbot/Supervisor.h\"" d)
  if(c EQUAL -1 AND d EQUAL -1)
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(all_sources "")
set(include_missing "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(APPEND all_sources "${t}\n")
  scope_file_ok("${t}" ok)
  if(NOT ok)
    file(RELATIVE_PATH rel "${PB_SOURCE_DIR}" "${f}")
    list(APPEND include_missing "${rel}")
  endif()
endforeach()

function(count_regex text regex out)
  string(REGEX MATCHALL "${regex}" hits "${text}")
  list(LENGTH hits n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# The text from the first `start` to the first `stop` after it ("" when start is missing).
function(region text start stop out)
  string(FIND "${text}" "${start}" at)
  if(at EQUAL -1)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "${stop}" end)
  if(end EQUAL -1)
    set(${out} "${rest}" PARENT_SCOPE)
  else()
    string(SUBSTRING "${rest}" 0 ${end} body)
    set(${out} "${body}" PARENT_SCOPE)
  endif()
endfunction()

# ---- the hook ------------------------------------------------------------------------------------------------------------
set(HOOK "    if (live_) ai::supervisor::ShadowObserve(ai, this);")
set(BAND_CLAMP "        statusTime = ai::quest_band::TripStatusMs(uint8(m_status), statusTime, bandTripEnd, uint32(time(nullptr)));")

function(check_hook travel_cpp all_sources out)
  count_regex("${all_sources}" "ShadowObserve\\(ai, " n)
  if(NOT n EQUAL 1)
    set(${out} "the observer is called ${n} times in the module (1 expected: TravelTarget::SetStatus)" PARENT_SCOPE)
    return()
  endif()
  region("${travel_cpp}" "void TravelTarget::SetStatus(TravelStatus status) {" "\n}\n" set_status)
  if(set_status STREQUAL "")
    set(${out} "TravelMgr.cpp: TravelTarget::SetStatus not found" PARENT_SCOPE)
    return()
  endif()
  string(LENGTH "${set_status}" len)
  string(LENGTH "${HOOK}" hook_len)
  if(len LESS hook_len)
    set(${out} "TravelMgr.cpp: SetStatus is shorter than the hook" PARENT_SCOPE)
    return()
  endif()
  math(EXPR tail_at "${len} - ${hook_len}")
  string(SUBSTRING "${set_status}" ${tail_at} -1 tail)
  if(NOT tail STREQUAL "${HOOK}")
    set(${out} "TravelMgr.cpp: the one-line observer is not the last statement of SetStatus" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${set_status}" "${BAND_CLAMP}" clamp)
  if(clamp EQUAL -1 OR NOT clamp LESS tail_at)
    set(${out} "TravelMgr.cpp: the observer is not after the band clamp" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the live target -----------------------------------------------------------------------------------------------------
function(check_live travel_h target_value_h all_sources out)
  foreach(needle "void MarkLive() { live_ = true; }" "bool live_ = false;")
    string(FIND "${travel_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "TravelMgr.h: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_regex("${all_sources}" "[^A-Za-z0-9_]live_ = " n)
  if(NOT n EQUAL 2)
    set(${out} "live_ is assigned ${n} times (2 expected: the member default and MarkLive)" PARENT_SCOPE)
    return()
  endif()
  # No ";" in a MATCHALL pattern: it would split the hit list and double the count.
  count_regex("${all_sources}" "->MarkLive\\(\\)" n)
  if(NOT n EQUAL 1)
    set(${out} "MarkLive is called ${n} times (1 expected: the TravelTargetValue constructor)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${target_value_h}" "ManualSetValue<TravelTarget*>(ai, new TravelTarget(ai), name) { value->MarkLive(); }" at)
  if(at EQUAL -1)
    set(${out} "TargetValue.h: the travel target value does not mark its own target live" PARENT_SCOPE)
    return()
  endif()
  # Nothing replaces the live pointer (Reset restores the same object, Value.h; design 8.1 point 3).
  # RESET_AI_VALUE (DebugAction) restores the same object and is allowed.
  count_regex("${all_sources}" "\"travel target\"\\)->Set\\(|[^A-Z_]SET_AI_VALUE\\(TravelTarget\\*" n)
  if(NOT n EQUAL 0)
    set(${out} "the live travel target pointer is replaced ${n} times (0 expected)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the shadow writes nothing -------------------------------------------------------------------------------------------
function(no_pointer name body out)
  if(body STREQUAL "")
    set(${out} "SupervisorShadow: struct ${name} not found" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${body}" "*" star)
  if(NOT star EQUAL -1)
    set(${out} "SupervisorShadow: struct ${name} keeps a pointer across passes" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_shadow shadow_h shadow_cpp out)
  set(code "${shadow_h}\n${shadow_cpp}")
  foreach(word
      "SetTarget" "SetStatus" "SetForced" "SetExpireIn" "SetConditions" "SetNullTravelTarget" "SetAIInternalUpdateDelay"
      "ChangeStrategy" "ResetStrategies" "DoSpecificAction" "TeleportTo" "->Set(" "->Reset(" "\"travel target\""
      "sPlayerbotAIConfig" "GetAiObjectContext" "EnsureSupervisor")
    string(FIND "${code}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "SupervisorShadow: the shadow must not use ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # Build rule 1: one file-local mutex around the budget, the sums and every bundle.
  count_regex("${shadow_cpp}" "std::mutex [A-Za-z_]+" n)
  if(NOT n EQUAL 1)
    set(${out} "SupervisorShadow.cpp: ${n} mutexes (1 expected)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${shadow_cpp}" "BudgetTake\\(" n)
  # A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): two places, CloseLocked and EmitEnforceLine (the enforcement lines
  # share the 300 per minute budget); the second one is pinned to sit under the lock below.
  if(NOT n EQUAL 2)
    set(${out} "SupervisorShadow.cpp: the line budget is taken in ${n} places (2 expected: CloseLocked and EmitEnforceLine, under the lock)" PARENT_SCOPE)
    return()
  endif()
  region("${shadow_cpp}" "void EmitEnforceLine(std::uint32_t guidLow, char const* text)" "\n}\n" enforce_line)
  string(FIND "${enforce_line}" "        std::lock_guard<std::mutex> lock(g_mutex);\n        taken = ai::rot_cast::BudgetTake(g_window, g_used, std::uint32_t(time(nullptr)), LinesPerMinute);" at)
  if(at EQUAL -1)
    set(${out} "SupervisorShadow.cpp: EmitEnforceLine does not take the budget under the lock" PARENT_SCOPE)
    return()
  endif()
  region("${shadow_cpp}" "Closed CloseLocked(" "\n}\n" close_locked)
  string(FIND "${close_locked}" "BudgetTake(g_window, g_used, nowSec, LinesPerMinute)" at)
  if(at EQUAL -1)
    set(${out} "SupervisorShadow.cpp: the budget is not taken in CloseLocked (seconds, 300 per minute)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${shadow_cpp}" "constexpr std::uint32_t LinesPerMinute = 300;" at)
  if(at EQUAL -1)
    set(${out} "SupervisorShadow.cpp: the line budget is not 300 per minute (E2 a)" PARENT_SCOPE)
    return()
  endif()
  # Build rule 3: the untagged / other-bot branch counts and returns before the owner's supervisor is read.
  region("${shadow_cpp}" "void ShadowObserve(PlayerbotAI* ai, TravelTarget* target)" "\n}\n" observe)
  if(observe STREQUAL "")
    set(${out} "SupervisorShadow.cpp: ShadowObserve not found" PARENT_SCOPE)
    return()
  endif()
  set(last -1)
  foreach(step
      "if (tag.kind == ThreadKind::None || (tag.kind == ThreadKind::Bot && tag.owner != ai))"
      "        std::lock_guard<std::mutex> lock(g_mutex);\n        target->NoteShadowGap();"
      "++g_min[cohortIdx].offthreadWrites;"
      "++g_min[cohortIdx].crossWrites;"
      "        return;\n    }"
      "ai->GetSupervisor()"
      "std::lock_guard<std::mutex> lock(g_mutex);\n    if (!state.on)")
    string(FIND "${observe}" "${step}" at)
    if(at EQUAL -1 OR NOT at GREATER last)
      set(${out} "SupervisorShadow.cpp: ShadowObserve order broken at: ${step}" PARENT_SCOPE)
      return()
    endif()
    set(last ${at})
  endforeach()
  count_regex("${shadow_cpp}" "GetSupervisor\\(\\)" n)
  if(NOT n EQUAL 1)
    set(${out} "SupervisorShadow.cpp: the supervisor is read in ${n} places (1 expected, after the tag check)" PARENT_SCOPE)
    return()
  endif()
  # Review 08.11.: no destination pointer is kept across passes (a temporary destination is deleted when replaced); the line
  # text is copied in ShadowObserve while the destination is alive and formatted from the copy.
  region("${shadow_h}" "struct DestKey" "\n};" dest_key)
  region("${shadow_h}" "struct ShadowState" "\n};" state_struct)
  region("${shadow_cpp}" "struct Closed" "\n};" closed_struct)
  no_pointer("DestKey" "${dest_key}" r)
  if(r STREQUAL "")
    no_pointer("ShadowState" "${state_struct}" r)
  endif()
  if(r STREQUAL "")
    no_pointer("Closed" "${closed_struct}" r)
  endif()
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  count_regex("${shadow_cpp}" "FillName\\(" n_all)
  count_regex("${observe}" "FillName\\(" n_observe)
  if(NOT n_all EQUAL 3 OR NOT n_observe EQUAL 2)
    set(${out} "SupervisorShadow.cpp: a destination name is read outside ShadowObserve (FillName ${n_all} in the file, ${n_observe} in ShadowObserve; 3 and 2 expected)" PARENT_SCOPE)
    return()
  endif()
  # The off path takes no lock while no shadow is on and never reads a state outside the lock.
  region("${shadow_cpp}" "void ShadowTick(PlayerbotAI* ai, ShadowState& state, bool shadowOn)" "\n}\n" tick)
  string(FIND "${tick}" "    if (!shadowOn)\n    {\n        // No shadow on anywhere: this state is empty too (only this bot's own tick turns it on), so no lock at the default.\n        if (!g_armed.load(std::memory_order_relaxed))\n            return;\n        std::lock_guard<std::mutex> lock(g_mutex);\n        DropLocked(state);\n        return;\n    }\n" at)
  if(at EQUAL -1)
    set(${out} "SupervisorShadow.cpp: the off path of ShadowTick is not: no lock while unarmed, else drop under the lock" PARENT_SCOPE)
    return()
  endif()
  # The last shadow off disarms the observer: every state reset goes through DropLocked.
  count_regex("${shadow_cpp}" "state = ShadowState\\(\\)" n)
  region("${shadow_cpp}" "void DropLocked(ShadowState& state)" "\n}\n" drop)
  string(FIND "${drop}" "g_armed.store(false, std::memory_order_relaxed);" disarm)
  if(NOT n EQUAL 1 OR disarm EQUAL -1)
    set(${out} "SupervisorShadow.cpp: a state is reset outside DropLocked, or the last shadow off does not disarm" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# Review 08.11.: every file that names a writer scope or a thread tag includes the shadow header (scope_file_ok, top).
function(check_includes missing extra out)
  if(NOT missing STREQUAL "")
    set(${out} "the shadow header is not included by: ${missing}" PARENT_SCOPE)
    return()
  endif()
  scope_file_ok("${extra}" ok)
  if(NOT ok)
    set(${out} "the shadow header is not included by a probed file" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the ratchet (armed) -------------------------------------------------------------------------------------------------
function(check_ratchet all_sources out)
  count_regex("${all_sources}" "(SetTarget|SetStatus|SetNullTravelTarget)\\(" n)
  if(n GREATER ${RATCHET_BASE})
    set(${out} "direct travel-target writer calls rose to ${n} (base ${RATCHET_BASE}): a new writer needs its family pair" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the scopes ----------------------------------------------------------------------------------------------------------
# Entries "<var>|<head>|<family>|<end of the function>": the scope line comes after the head, before the function's end and
# before any writer call text in between.
set(END_FN "\n}\n")
set(SCOPES
  "choose_cpp|bool ChooseTravelTargetAction::Execute(Event& event)|Select|${END_FN}"
  "choose_cpp|bool RefreshTravelTargetAction::Execute(Event& event)|Select|${END_FN}"
  "choose_cpp|bool ResetTargetAction::Execute(Event& event)|Select|${END_FN}"
  "choose_cpp|bool RequestTravelTargetAction::Execute(Event& event)|Requester|${END_FN}"
  "choose_cpp|bool RequestNamedTravelTargetAction::Execute(Event& event)|Requester|${END_FN}"
  "choose_cpp|bool RequestQuestTravelTargetAction::Execute(Event& event)|Requester|${END_FN}"
  "choose_cpp|bool ChooseGroupTravelTargetAction::Execute(Event& event)|GroupFollow|${END_FN}"
  "band_cpp|bool ChooseTravelTargetAction::TryBandStep(|Select|${END_FN}"
  "band_cpp|bool ChooseTravelTargetAction::StartBandWalk(|Select|${END_FN}"
  "group_world_cpp|bool ForceQuestTravel(Player* bot, std::uint32_t questId, int objIdx)|Select|${END_FN}"
  "group_world_cpp|bool RestoreTravel(PlayerbotAI* ai, SavedTravel const& s)|Select|${END_FN}"
  "move_cpp|bool MoveToTravelTargetAction::Execute(Event& event)|Execution|${END_FN}"
  "travel_cpp|void TravelTarget::CheckStatus()|Execution|${END_FN}"
  "travel_cpp|bool TravelTarget::MarkUnreachableCamp(uint32 quest, int32 entry, uint32 markMs)|Execution|${END_FN}"
  "travel_cpp|void TravelTarget::OnDeathOnTurnInRoute()|Lifecycle|${END_FN}"
  "travel_cpp|void TravelTarget::OnDeathAtDestination()|Lifecycle|${END_FN}"
  "travel_cpp|void TravelMgr::Clear()|Manager|${END_FN}"
  "travel_values_cpp|void ai::ProfessionTripTick(PlayerbotAI* ai)|Errand|${END_FN}"
  "release_h|class RepopAction : public SpiritHealerAction|Lifecycle|\n        }\n"
  "escape_cpp|bool ZoneEscapeAction::Execute(Event& event)|Lifecycle|${END_FN}"
  "station_cpp|bool StationCraftTrigger::IsActive()|Errand|${END_FN}"
  "guild_cpp|bool PetitionTurnInAction::Execute(Event& event)|Errand|${END_FN}"
  "guild_cpp|bool RosterGuildFoundTripAction::Execute(Event& event)|Errand|${END_FN}"
  "rnd_cpp|void RandomPlayerbotMgr::UpdateBgFillMembers()|Manager|${END_FN}"
  "rnd_cpp|void RandomPlayerbotMgr::RandomTeleportForRpg(Player* bot, bool activeOnly)|Manager|${END_FN}"
  "rnd_cpp|bool RandomPlayerbotMgr::ParkBot(Player* bot, std::string const& teleName, std::string& reason)|Manager|${END_FN}"
  "rnd_cpp|bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args)|Manager|${END_FN}"
  "rnd_cpp|void RandomPlayerbotMgr::RandomTeleport(Player* bot, std::vector<WorldLocation> &locs, bool hearth, bool activeOnly)|Manager|${END_FN}"
  "rnd_cpp|void RandomPlayerbotMgr::ProcessQuestRescues()|Manager|${END_FN}"
  "follow_cpp|bool FollowAction::Execute(Event& event)|GroupFollow|${END_FN}"
  "worldbuff_cpp|bool WorldBuffTravelApplyAction::Execute(Event& event)|GroupFollow|${END_FN}"
  "worldbuff_cpp|bool WorldBuffTravelSetTargetAction::Execute(Event& event)|GroupFollow|${END_FN}"
  "go_cpp|bool GoAction::TravelTo(TravelDestination* dest, Player* requester) const|Command|${END_FN}"
  "debug_cpp|bool DebugAction::HandleStuck(Event& event, Player* requester, const std::string& text)|Command|${END_FN}"
  "setup_cpp|TestResult CommandSetupSetDestination::Execute(|Test|${END_FN}")

set(WRITER_TOKENS "SetStatus(" "SetTarget(" "SetNullTravelTarget(" "setNewTarget(" "CopyTarget(" "Reset(")

# The file texts come from the caller's scope by name (${${var}}); a negative probe swaps one of them: probe_var names the
# variable, probe_text is its mutated text ("" "" = the real sources).
function(check_scopes probe_var probe_text out)
  foreach(entry IN LISTS SCOPES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 var)
    list(GET parts 1 head)
    list(GET parts 2 family)
    list(GET parts 3 stop)
    if(var STREQUAL probe_var)
      set(text "${probe_text}")
    else()
      set(text "${${var}}")
    endif()
    string(FIND "${text}" "${head}" head_at)
    if(head_at EQUAL -1)
      set(${out} "${var}: entry function not found: ${head}" PARENT_SCOPE)
      return()
    endif()
    string(LENGTH "${head}" head_len)
    math(EXPR body_at "${head_at} + ${head_len}")
    string(SUBSTRING "${text}" ${body_at} -1 body)
    string(FIND "${body}" "${stop}" end)
    set(line "ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::${family});")
    string(FIND "${body}" "${line}" scope)
    if(scope EQUAL -1 OR (NOT end EQUAL -1 AND NOT scope LESS end))
      set(${out} "${var}: ${head} has no ${family} writer scope" PARENT_SCOPE)
      return()
    endif()
    string(SUBSTRING "${body}" 0 ${scope} before)
    foreach(token IN LISTS WRITER_TOKENS)
      string(FIND "${before}" "${token}" w)
      if(NOT w EQUAL -1)
        set(${out} "${var}: ${head}: a writer call (${token}) before the scope" PARENT_SCOPE)
        return()
      endif()
    endforeach()
  endforeach()
  set(text "${choose_cpp}")
  if(probe_var STREQUAL "choose_cpp")
    set(text "${probe_text}")
  endif()
  string(FIND "${text}" "bool ChooseTravelTargetAction::setNewTarget(Player* requester, TravelTarget* newTarget, TravelTarget* oldTarget)\n{\n    // A3 step 3 arbiter shadow: every module caller has its own scope; an empty stack here is the bot brain's override.\n    ai::supervisor::WriterScope const writerScope = ai::supervisor::WriterScope::IfEmpty(ai::supervisor::WriterId::Brain);\n" at)
  if(at EQUAL -1)
    set(${out} "ChooseTravelTargetAction.cpp: setNewTarget lacks the Brain fallback as its first statement" PARENT_SCOPE)
    return()
  endif()
  set(text "${ai_cpp}")
  if(probe_var STREQUAL "ai_cpp")
    set(text "${probe_text}")
  endif()
  foreach(needle
      "void PlayerbotAI::Reset(bool full)\n{\n    // A3 step 3 arbiter shadow: a reset is a lifecycle write unless a caller (the manager's teleport or rescue) named its family.\n    ai::supervisor::WriterScope const writerScope = ai::supervisor::WriterScope::IfEmpty(ai::supervisor::WriterId::Lifecycle);\n"
      "                    ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Lifecycle);\n                    TravelTarget* travelTarget = aiObjectContext->GetValue<TravelTarget*>(\"travel target\")->Get();")
    string(FIND "${text}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAI.cpp: missing scope: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # No scope on the whole of UpdateAI: it would hide every Unknown writer of the pass.
  region("${text}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)\n{" "    HandleQueuedBotPackets();" ua_head)
  string(FIND "${ua_head}" "WriterScope" at)
  if(NOT at EQUAL -1)
    set(${out} "PlayerbotAI.cpp: a writer scope on the whole of UpdateAI" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# Every setNewTarget caller in the module is a scoped function: the known five call sites, plus the declaration and the
# definition. A new caller fails here and needs its scope (or the Brain fallback would claim it).
function(check_new_target_callers all_sources out)
  count_regex("${all_sources}" "setNewTarget\\(" n)
  if(NOT n EQUAL 7)
    set(${out} "setNewTarget( appears ${n} times (7 expected: 5 scoped callers, declaration, definition)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the thread tags -----------------------------------------------------------------------------------------------------
function(check_tags ai_cpp scripts_cpp rnd_cpp all_sources out)
  string(FIND "${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)\n{\n    // A3 step 3 arbiter shadow (AiPlayerbot.Supervisor.Arbiter, default 0): writes on this thread belong to this bot.\n    ai::supervisor::ThreadTag const threadTag(ai::supervisor::ThreadKind::Bot, this);\n" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAI.cpp: the bot thread tag is not the first statement of UpdateAI" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${scripts_cpp}" "        void OnUpdate(uint32 diff) override\n        {\n            // A3 step 3 arbiter shadow (AiPlayerbot.Supervisor.Arbiter): writes here run with all maps idle.\n            ai::supervisor::ThreadTag const threadTag(ai::supervisor::ThreadKind::World, nullptr);\n" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotScripts.cpp: the world thread tag is not the first statement of the world hook" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${rnd_cpp}" "bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args)\n{\n    // A3 step 3 arbiter shadow: console and GM chat run on the world thread (CLI-GRP 07.11. (i)); a tag already set stays.\n    ai::supervisor::ThreadTag const threadTag(ai::supervisor::ThreadKind::World, nullptr, true);\n" at)
  if(at EQUAL -1)
    set(${out} "RandomPlayerbotMgr.cpp: the console thread tag (keeping an outer tag) is not the first statement" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "ThreadTag const threadTag\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "${n} thread tags in the module (3 expected)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the switch ----------------------------------------------------------------------------------------------------------
set(ARB_GATE "        if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, bot->GetGUIDLow()))\n            EnsureSupervisor()->Tick(WorldTimer::getMSTime(), ai::supervisor::ArbiterValue(sPlayerbotAIConfig.supervisorArbiter));\n")

function(check_switch config_h config_cpp conf_dist ai_cpp policy all_sources out)
  string(FIND "${config_h}" "    uint32 supervisorArbiter = 0;" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.h: member not default 0: uint32 supervisorArbiter = 0;" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "    supervisorArbiter = uint32(config.GetIntDefault(\"AiPlayerbot.Supervisor.Arbiter\", 0));\n    if (!ai::supervisor::ValidArbiter(supervisorArbiter))\n    {\n        sLog.outError(\"[Supervisor] config key=Supervisor.Arbiter value=%u result=off (0 off, 1 shadow, 2 enforce)\", supervisorArbiter);\n        supervisorArbiter = 0;\n    }"
      "            {\"Supervisor\", Value(supervisor)},\n            {\"Supervisor.Arbiter\", Value(supervisorArbiter)},\n            {\"Supervisor.ArbiterProbe\", Value(supervisorArbiterProbe)},\n            {\"ThreadSafeTeleportAck\"")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${policy}" "inline bool ValidArbiter(std::uint32_t value)\n{\n    return value <= 2;\n}" at)
  if(at EQUAL -1)
    set(${out} "SupervisorPolicy.h: ValidArbiter is not 0, 1 or 2 only" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "\nAiPlayerbot\\.Supervisor\\.Arbiter = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.Supervisor.Arbiter = 0")
    set(${out} "conf.dist: AiPlayerbot.Supervisor.Arbiter not documented once as 0" PARENT_SCOPE)
    return()
  endif()
  # Read only at the gate, under the parent switch and its cohort.
  string(FIND "${ai_cpp}" "${ARB_GATE}" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAI.cpp: the arbiter switch is not read at the supervisor gate, parent switch first" PARENT_SCOPE)
    return()
  endif()
  # A3 step 3 T02-a: the probe key (supervisorArbiterProbe) is a different member; it is pinned by the enforce contract.
  # CLI-GRP T30-1c (agreed with CLI-Projekt 10.11.): the group manager reads the switch through ArbiterValue in LiveTypesNow
  # (GroupManager.cpp; pinned by a3s6_t30_directives check_t30_1c), so 2.
  count_regex("${all_sources}" "sPlayerbotAIConfig\\.supervisorArbiter[^A-Za-z0-9_]" n)
  if(NOT n EQUAL 2)
    set(${out} "the arbiter switch is read in ${n} places (2 expected: the gate, the group manager)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- dormant paths -------------------------------------------------------------------------------------------------------
# CLI-GRP 07.11. (ii): RandomBotUpdateAction would run the manager's ProcessBot on a map thread; it stays dormant while nothing
# sets "random bot update" (a ManualSetValue, default false).
function(check_dormant all_sources out)
  count_regex("${all_sources}" "\"random bot update\"\\)->Set\\(|[^A-Z_]SET_AI_VALUE\\(bool, \"random bot update\"" n)
  if(NOT n EQUAL 0)
    set(${out} "\"random bot update\" is set in ${n} places (0 expected: the map-thread manager path stays dormant)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${all_sources}" "RandomBotUpdateValue(PlayerbotAI* ai, std::string name = \"random bot update\") : ManualSetValue<bool>(ai, false, name) {}" at)
  if(at EQUAL -1)
    set(${out} "RandomBotUpdateValue.h: \"random bot update\" is no longer a ManualSetValue with default false" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the unit test -------------------------------------------------------------------------------------------------------
function(check_unit unit_test out)
  foreach(needle "void TestArbiter()" "    TestArbiter();\n")
    string(FIND "${unit_test}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "t/supervisor_policy_tests.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- run on the real sources ---------------------------------------------------------------------------------------------
check_hook("${travel_cpp}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_live("${travel_h}" "${target_value_h}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_shadow("${shadow_h}" "${shadow_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_ratchet("${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_scopes("" "" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_new_target_callers("${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_includes("${include_missing}" "" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_tags("${ai_cpp}" "${scripts_cpp}" "${rnd_cpp}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" "${ai_cpp}" "${policy}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_dormant("${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_unit("${unit_test}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
count_regex("${all_sources}" "(SetTarget|SetStatus|SetNullTravelTarget)\\(" writers)
message(STATUS "${TAG}: direct travel-target writer calls in the module: ${writers} (ratchet armed at <= ${RATCHET_BASE})")

# ---- negative probes: each mutation must be caught -----------------------------------------------------------------------
function(expect_caught result description)
  if(result STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${description}")
  endif()
endfunction()

# Caught for the right reason: the result names `want` (review 08.11.: a probe caught by an earlier check proves nothing).
function(expect_reason result want description)
  string(FIND "${result}" "${want}" at)
  if(result STREQUAL "" OR at EQUAL -1)
    message(FATAL_ERROR "${TAG}: negative probe not caught for its reason (${want}): ${description}; got: ${result}")
  endif()
endfunction()

string(REPLACE "${HOOK}" "    if (live_)\n    {\n        ai::supervisor::ShadowObserve(ai, this);\n    }" m "${travel_cpp}")
check_hook("${m}" "${all_sources}" r)
expect_caught("${r}" "the observer in a block")

string(REPLACE "${HOOK}\n}" "}" m "${travel_cpp}")
string(REPLACE "    m_status = status;\n" "    m_status = status;\n${HOOK}\n" m "${m}")
check_hook("${m}" "${all_sources}" r)
expect_reason("${r}" "last statement" "the observer before the status time and the band clamp")

string(REPLACE "${BAND_CLAMP}\n" "" m "${travel_cpp}")
check_hook("${m}" "${all_sources}" r)
expect_reason("${r}" "band clamp" "the band clamp gone, the observer still last")

string(REPLACE "    DestKey last;\n" "    ai::TravelDestination const* lastDest = nullptr;\n" m "${shadow_h}")
check_shadow("${m}" "${shadow_cpp}" r)
expect_reason("${r}" "keeps a pointer" "a destination pointer kept in the state (use after free)")

string(REPLACE "    DestKey from;\n    DestKey to;\n" "    DestKey from;\n    TravelDestination const* to = nullptr;\n" m "${shadow_cpp}")
check_shadow("${shadow_h}" "${m}" r)
expect_reason("${r}" "keeps a pointer" "a destination pointer in the closed bundle")

string(REPLACE "    ShadowLine line;\n" "    FillName(key, dest);\n    ShadowLine line;\n" m "${shadow_cpp}")
check_shadow("${shadow_h}" "${m}" r)
expect_reason("${r}" "outside ShadowObserve" "a destination read when the line is written")

string(REPLACE "        std::lock_guard<std::mutex> lock(g_mutex);\n        target->NoteShadowGap();\n" "        target->NoteShadowGap();\n        std::lock_guard<std::mutex> lock(g_mutex);\n" m "${shadow_cpp}")
check_shadow("${shadow_h}" "${m}" r)
expect_reason("${r}" "NoteShadowGap" "the gap mark written outside the lock")

string(REPLACE "        std::lock_guard<std::mutex> lock(g_mutex);\n        DropLocked(state);\n        return;\n" "        DropLocked(state);\n        return;\n" m "${shadow_cpp}")
check_shadow("${shadow_h}" "${m}" r)
expect_reason("${r}" "off path" "the off path drops the state without the lock")

string(REPLACE "        g_armed.store(false, std::memory_order_relaxed);\n" "" m "${shadow_cpp}")
check_shadow("${shadow_h}" "${m}" r)
expect_reason("${r}" "disarm" "the observer stays armed after the last shadow is off")

string(REPLACE "#include \"playerbot/SupervisorShadow.h\"   // A3 step 3 arbiter shadow: writer scopes\n" "" m "${worldbuff_cpp}")
check_includes("" "${m}" r)
expect_reason("${r}" "not included" "a scoped file without the shadow header")

check_hook("${travel_cpp}" "${all_sources}\n    ai::supervisor::ShadowObserve(ai, this);" r)
expect_caught("${r}" "a second observer (e.g. in SetTarget)")

check_live("${travel_h}" "${target_value_h}" "${all_sources}\n    value->MarkLive();" r)
expect_caught("${r}" "MarkLive twice")

check_live("${travel_h}" "${target_value_h}" "${all_sources}\n    copy->live_ = true;" r)
expect_caught("${r}" "live_ written outside MarkLive")

string(REPLACE " { value->MarkLive(); }" " {}" m "${target_value_h}")
check_live("${travel_h}" "${m}" "${all_sources}" r)
expect_caught("${r}" "the live target not marked")

check_live("${travel_h}" "${target_value_h}" "${all_sources}\n    context->GetValue<TravelTarget*>(\"travel target\")->Set(other);" r)
expect_caught("${r}" "the live pointer replaced")

check_shadow("${shadow_h}" "${shadow_cpp}\n    target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" r)
expect_caught("${r}" "a writer in the shadow")

check_shadow("${shadow_h}" "${shadow_cpp}\n    bool on = sPlayerbotAIConfig.supervisorArbiter;" r)
expect_caught("${r}" "a config read in the shadow")

check_shadow("${shadow_h}" "${shadow_cpp}\nstd::mutex g_other;" r)
expect_caught("${r}" "a second mutex")

string(REPLACE "    Supervisor* const supervisor = ai->GetSupervisor();\n" "" m "${shadow_cpp}")
string(REPLACE "    Tag const tag = t_tag;\n" "    Tag const tag = t_tag;\n    Supervisor* const supervisor = ai->GetSupervisor();\n" m "${m}")
check_shadow("${shadow_h}" "${m}" r)
expect_caught("${r}" "the untagged branch reads the owner's supervisor (use after free)")

string(REPLACE "    std::lock_guard<std::mutex> lock(g_mutex);\n    if (!state.on)" "    if (!state.on)" m "${shadow_cpp}")
check_shadow("${shadow_h}" "${m}" r)
expect_caught("${r}" "the bundle written without the lock")

check_ratchet("${all_sources}\n    target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" r)
expect_caught("${r}" "a new direct travel-target writer")

string(REPLACE "void TravelTarget::CheckStatus()\n{\n    ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Execution);\n" "void TravelTarget::CheckStatus()\n{\n" m "${travel_cpp}")
check_scopes("travel_cpp" "${m}" r)
expect_caught("${r}" "the scope removed from CheckStatus")

string(REPLACE "    ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Requester);\n    // #416: no new destination job while the parking lot is full.\n" "    // #416: no new destination job while the parking lot is full.\n" m "${choose_cpp}")
check_scopes("choose_cpp" "${m}" r)
expect_caught("${r}" "a requester scope removed")

string(REPLACE "ai::supervisor::WriterScope::IfEmpty(ai::supervisor::WriterId::Brain)" "ai::supervisor::WriterScope(ai::supervisor::WriterId::Brain)" m "${choose_cpp}")
check_scopes("choose_cpp" "${m}" r)
expect_caught("${r}" "setNewTarget overrides its callers' scope")

string(REPLACE "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)\n{\n" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)\n{\n    ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Lifecycle);\n" m "${ai_cpp}")
check_scopes("ai_cpp" "${m}" r)
expect_caught("${r}" "a scope on the whole of UpdateAI")

check_new_target_callers("${all_sources}\n    setNewTarget(requester, &other, travelTarget);" r)
expect_caught("${r}" "a new setNewTarget caller")

string(REPLACE "    ai::supervisor::ThreadTag const threadTag(ai::supervisor::ThreadKind::Bot, this);\n" "" m "${ai_cpp}")
check_tags("${m}" "${scripts_cpp}" "${rnd_cpp}" "${all_sources}" r)
expect_caught("${r}" "no bot thread tag")

string(REPLACE "(ai::supervisor::ThreadKind::World, nullptr, true)" "(ai::supervisor::ThreadKind::World, nullptr)" m "${rnd_cpp}")
check_tags("${ai_cpp}" "${scripts_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "the console tag overrides a bot's tag")

string(REPLACE "AiPlayerbot.Supervisor.Arbiter = 0" "AiPlayerbot.Supervisor.Arbiter = 1" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" "${ai_cpp}" "${policy}" "${all_sources}" r)
expect_caught("${r}" "conf.dist default 1")

string(REPLACE "uint32 supervisorArbiter = 0;" "uint32 supervisorArbiter = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" "${ai_cpp}" "${policy}" "${all_sources}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "    if (!ai::supervisor::ValidArbiter(supervisorArbiter))\n" "    if (false)\n" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" "${ai_cpp}" "${policy}" "${all_sources}" r)
expect_caught("${r}" "no clamp")

string(REPLACE "    return value <= 2;" "    return value <= 3;" m "${policy}")
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" "${ai_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "a value above 2 let through")

string(REPLACE "            {\"Supervisor.Arbiter\", Value(supervisorArbiter)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" "${ai_cpp}" "${policy}" "${all_sources}" r)
expect_caught("${r}" "not in [ConfigEcho]")

string(REPLACE "ai::supervisor::ArbiterValue(sPlayerbotAIConfig.supervisorArbiter));" "true);" m "${ai_cpp}")
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" "${m}" "${policy}" "${all_sources}" r)
expect_caught("${r}" "the shadow on without its switch")

check_switch("${config_h}" "${config_cpp}" "${conf_dist}" "${ai_cpp}" "${policy}" "${all_sources}\n    if (sPlayerbotAIConfig.supervisorArbiter)" r)
expect_caught("${r}" "the switch read outside the gate")

check_dormant("${all_sources}\n    SET_AI_VALUE(bool, \"random bot update\", true);" r)
expect_caught("${r}" "the map-thread manager path woken")

string(REPLACE "    TestArbiter();\n" "" m "${unit_test}")
check_unit("${m}" r)
expect_caught("${r}" "the arbiter unit test not run")

message(STATUS "${TAG}: OK")
