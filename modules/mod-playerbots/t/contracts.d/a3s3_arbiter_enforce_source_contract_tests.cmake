cmake_policy(VERSION 3.16)
# A3 step 3, behaviour pair T02-a (ADR-0041; AiPlayerbot.Supervisor.Arbiter = 2 enforce, default 0; design
# ws-00/projekt-durchleuchtung/A3-S3-T02-ENTWURF.md v0.3, build spec A3-S3-T02A-BAUSPEC.md): the enforcing arbiter on the live
# travel target. At the defaults (AiPlayerbot.Supervisor = 0, or Supervisor.Arbiter < 2) every path behaves as before. This
# contract pins that the new gate cannot be bypassed or leak into the defaults:
#   (a) the gate: AdmitTargetWrite( is called exactly twice in TravelMgr.cpp, as the first statement of TravelTarget::SetTarget and
#       as the first statement(s) of CopyTarget (arbiterDenied_ reset, then the gate, before the #485 check); the old body lives in
#       SetTargetUnchecked and holds no gate; the gate reads the supervisor only on the bot's own map thread;
#   (b) the destination generation: writeGen_ is raised once (fetch_add in SetTargetUnchecked, before its SetStatus), nowhere stored;
#   (c) every call of SetTarget / setNewTarget / CopyTarget in the module is either an explicit live-target site that evaluates
#       the result, or an excluded local-target line; a new call fails here until it is listed (per-file census);
#   (d) LaneFree( call sites exactly: Request isUseful, the four errands (petition, guild_trip twice, profession_trip,
#       station_craft) and the probe; Errand writers use WriterId::Errand;
#   (e) the closers census: every SetStatus(EXPIRED|COOLDOWN|NONE) outside TravelMgr.cpp, per file; a new one fails;
#   (f) the probe runs only under AiPlayerbot.Supervisor.ArbiterProbe (default 0, echoed, documented);
#   (g) ~Supervisor and Supervisor.h/.cpp hold no gate call (the destructor runs on any thread);
#   (h) ValidArbiter accepts 0, 1 and 2 only; the console takes enforce;
#   (i) negative probes: each pin must fail on a mutated copy;
#   (j) A3 step 3 T02-GROUP (BotGroups.Manager.Types > 0, default 0): the no-supervisor branch of AdmitTargetWrite counts a write
#       that bypasses the gate (GateOffThread, one call, first statement the arbiter != 2 return); the snapshot / filter helpers
#       start from EnforcingSupervisor; travel code never reads GetSupervisor; SupervisorGroup.cpp adds one live write (the
#       point) and two closers; SetOwner(Lane::Group, once in the module.
# Pure rules: t/supervisor_policy_tests.cpp (TestArbiterLanes). Every check_* returns "" when it holds, else the reason; the
# negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s3 arbiter enforce: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s3 arbiter enforce: PB_MODULE_DIR is required")
endif()

set(TAG "a3s3 arbiter enforce")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("TravelMgr.cpp" travel_cpp)
read_source("TravelMgr.h" travel_h)
read_source("SupervisorEnforce.cpp" enforce_cpp)
read_source("SupervisorEnforce.h" enforce_h)
read_source("Supervisor.cpp" sup_cpp)
read_source("Supervisor.h" sup_h)
read_source("SupervisorPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("strategy/actions/GuildCreateActions.cpp" guild_cpp)
read_source("strategy/triggers/StationCraftTrigger.cpp" station_cpp)
read_source("strategy/values/TravelValues.cpp" travel_values_cpp)
file(READ "${PB_MODULE_DIR}/t/supervisor_policy_tests.cpp" unit_test)
string(REPLACE "\r" "" unit_test "${unit_test}")

# ";" would split the hit list of a MATCHALL, so the texts are counted with ";" replaced.
function(count_regex text regex out)
  string(REPLACE ";" "@" flat "${text}")
  string(REGEX MATCHALL "${regex}" hits "${flat}")
  list(LENGTH hits n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# The text from the first `start` to the first `stop` after it ("" when start is missing; the rest when stop is missing).
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

# Number of non-overlapping occurrences of a plain needle.
function(count_text text needle out)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" nl)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${nl}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# All module sources, and per file the census numbers used below.
file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(SORT all_files)
set(all_sources "")
set(call_census "")
set(closer_census "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(REPLACE "\r" "" t "${t}")
  string(APPEND all_sources "${t}\n")
  file(RELATIVE_PATH rel "${PB_SOURCE_DIR}" "${f}")
  count_regex("${t}" "(SetTarget|setNewTarget|CopyTarget)\\(" n_all)
  count_regex("${t}" "[A-Za-z0-9_](SetTarget|setNewTarget|CopyTarget)\\(" n_ident)
  math(EXPR n_calls "${n_all} - ${n_ident}")
  if(n_calls GREATER 0)
    list(APPEND call_census "${rel}=${n_calls}")
  endif()
  if(NOT rel STREQUAL "TravelMgr.cpp")
    count_regex("${t}" "SetStatus\\(TravelStatus::TRAVEL_STATUS_(EXPIRED|COOLDOWN|NONE)\\)" n_close)
    if(n_close GREATER 0)
      list(APPEND closer_census "${rel}=${n_close}")
    endif()
  endif()
endforeach()

# ---- (a) the gate ------------------------------------------------------------------------------------------------------------
set(SET_TARGET_HEAD "bool TravelTarget::SetTarget(TravelDestination* tDestination1, WorldPosition* wPosition1) {\n    if (live_ && !ai::supervisor::AdmitTargetWrite(ai, this, IsNullDest(tDestination1), \"set_target\"))\n")
set(COPY_TARGET_HEAD "bool TravelTarget::CopyTarget(TravelTarget* const target) {\n    arbiterDenied_ = false;\n    if (live_ && !ai::supervisor::AdmitTargetWrite(ai, this, IsNullDest(target->tDestination), \"copy_target\"))\n")

function(check_gate travel_cpp enforce_cpp enforce_h all_sources out)
  count_regex("${travel_cpp}" "[^A-Za-z]AdmitTargetWrite\\(" n)
  if(NOT n EQUAL 2)
    set(${out} "TravelMgr.cpp: AdmitTargetWrite( appears ${n} times (2 expected: SetTarget and CopyTarget)" PARENT_SCOPE)
    return()
  endif()
  # Module-wide: the two calls, the definition and the declaration; no other caller.
  count_regex("${all_sources}" "[^A-Za-z]AdmitTargetWrite\\(" n)
  if(NOT n EQUAL 4)
    set(${out} "AdmitTargetWrite( appears ${n} times in the module (4 expected: 2 calls, definition, declaration)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${travel_cpp}" "${SET_TARGET_HEAD}" at)
  if(at EQUAL -1)
    set(${out} "TravelMgr.cpp: the gate is not the first statement of TravelTarget::SetTarget (live target only)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${travel_cpp}" "${COPY_TARGET_HEAD}" at)
  if(at EQUAL -1)
    set(${out} "TravelMgr.cpp: the gate is not the first statement of TravelTarget::CopyTarget (after the arbiterDenied_ reset)" PARENT_SCOPE)
    return()
  endif()
  # The gate comes before the #485 refusal; a denied copy writes nothing (the head returns before the old body).
  region("${travel_cpp}" "bool TravelTarget::CopyTarget(TravelTarget* const target) {" "\n}\n" copy_target)
  string(FIND "${copy_target}" "AdmitTargetWrite(" gate_at)
  string(FIND "${copy_target}" "TraceQuestCommit(taker, \"drop\", \"parked_intent\");" park_at)
  string(FIND "${copy_target}" "SetTargetUnchecked(target->tDestination, target->wPosition);" copy_at)
  if(gate_at EQUAL -1 OR park_at EQUAL -1 OR copy_at EQUAL -1 OR NOT gate_at LESS park_at OR NOT park_at LESS copy_at)
    set(${out} "TravelMgr.cpp: CopyTarget order broken (gate, then the #485 refusal, then SetTargetUnchecked)" PARENT_SCOPE)
    return()
  endif()
  # The old body carries no gate (it would double-count and could deny a copy half-way).
  region("${travel_cpp}" "void TravelTarget::SetTargetUnchecked(TravelDestination* tDestination1, WorldPosition* wPosition1) {" "\n}\n" unchecked)
  if(unchecked STREQUAL "")
    set(${out} "TravelMgr.cpp: TravelTarget::SetTargetUnchecked not found" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${unchecked}" "AdmitTargetWrite" at)
  if(NOT at EQUAL -1)
    set(${out} "TravelMgr.cpp: SetTargetUnchecked holds a gate" PARENT_SCOPE)
    return()
  endif()
  # Two-step owner (review 10.11.): the gate only stages, the write commits right after SetTargetUnchecked - in the public write and
  # in CopyTarget, nowhere else - so a gate pass without a write (the #485 refusal) leaves no owner.
  count_regex("${travel_cpp}" "CommitTargetWrite\\(" commits)
  string(FIND "${travel_cpp}" "    SetTargetUnchecked(tDestination1, wPosition1);\n    if (live_)\n        ai::supervisor::CommitTargetWrite(ai, this);\n    return true;" c1)
  string(FIND "${travel_cpp}" "    SetTargetUnchecked(target->tDestination, target->wPosition);   // A3 step 3 T02-a: the gate ran at the head\n    if (live_)\n        ai::supervisor::CommitTargetWrite(ai, this);\n" c2)
  if(NOT commits EQUAL 2 OR c1 EQUAL -1 OR c2 EQUAL -1)
    set(${out} "TravelMgr.cpp: the owner commit is not exactly once right after each gated write (found ${commits})" PARENT_SCOPE)
    return()
  endif()
  region("${enforce_cpp}" "bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site)" "\n}\n" admit_body)
  string(FIND "${admit_body}" "SetOwner(" at)
  if(NOT at EQUAL -1)
    set(${out} "SupervisorEnforce.cpp: the gate sets the owner before the write (stage only, commit after)" PARENT_SCOPE)
    return()
  endif()
  # Thread safety: the supervisor is read only after the bot's own map thread is proven (SupervisorShadow.h OnOwnThread).
  region("${enforce_cpp}" "Supervisor* EnforcingSupervisor(PlayerbotAI* ai)" "\n}\n" enforcing)
  string(FIND "${enforcing}" "if (!ai || !ai->GetBot() || !OnOwnThread(ai))\n        return nullptr;\n    Supervisor* const supervisor = ai->GetSupervisor();\n    return supervisor && supervisor->EnforceOn() ? supervisor : nullptr;" at)
  if(at EQUAL -1)
    set(${out} "SupervisorEnforce.cpp: the gate does not check the own map thread before it reads the supervisor, or does not return at once when enforcement is off" PARENT_SCOPE)
    return()
  endif()
  count_regex("${enforce_cpp}" "GetSupervisor\\(\\)" n)
  if(NOT n EQUAL 1)
    set(${out} "SupervisorEnforce.cpp: the supervisor is read in ${n} places (1 expected: EnforcingSupervisor, after the thread check)" PARENT_SCOPE)
    return()
  endif()
  # Every public function starts from EnforcingSupervisor (an early allow at the defaults).
  foreach(fn "bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return GateOffThread(ai, target, nullDest);\n    if (!target || !target->IsLive())\n        return true;"
      "bool WouldAdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor || !target || !target->IsLive())\n        return true;"
      "bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return true;")
    string(FIND "${enforce_cpp}" "${fn}" at)
    if(at EQUAL -1)
      set(${out} "SupervisorEnforce.cpp: a gate function does not allow at once without an enforcing supervisor" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0): GateOffThread is the
  # counter for a write that bypasses the gate because no enforcing supervisor is on this thread. It is called once, in
  # AdmitTargetWrite's no-supervisor branch (after the `!supervisor` test, never before EnforcingSupervisor), never in the
  # WouldAdmit/LaneFree gates; its body reads no supervisor (it runs on any thread) and starts with the arbiter value test.
  region("${enforce_cpp}" "bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site)\n{" "\n}\n" admit_fn)
  region("${enforce_cpp}" "bool WouldAdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest)\n{" "\n}\n" would_fn)
  region("${enforce_cpp}" "bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site)\n{" "\n}\n" lane_fn)
  region("${enforce_cpp}" "bool GateOffThread(PlayerbotAI* ai, TravelTarget* target, bool nullDest)\n{" "\n}\n" gateoff_fn)
  count_text("${admit_fn}" "GateOffThread(" n_a)
  count_text("${would_fn}" "GateOffThread(" n_w)
  count_text("${lane_fn}" "GateOffThread(" n_l)
  if(gateoff_fn STREQUAL "" OR NOT n_a EQUAL 1 OR NOT n_w EQUAL 0 OR NOT n_l EQUAL 0)
    set(${out} "SupervisorEnforce.cpp: GateOffThread( must be defined and called once, in AdmitTargetWrite only (found ${n_a} there, ${n_w} in WouldAdmitTargetWrite, ${n_l} in LaneFree)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${gateoff_fn}" "GetSupervisor" at)
  string(FIND "${gateoff_fn}" "{\n    if (ArbiterValue(sPlayerbotAIConfig.supervisorArbiter) != 2)\n        return true;\n" at3)
  if(NOT at EQUAL -1 OR at3 EQUAL -1)
    set(${out} "SupervisorEnforce.cpp: GateOffThread reads the supervisor or does not start with the arbiter != 2 early return" PARENT_SCOPE)
    return()
  endif()
  # The two region entry points read the supervisor snapshot only through EnforcingSupervisor (own thread proven, enforce on).
  foreach(fn "std::uint32_t GroupGoalSnapshot(PlayerbotAI* ai)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n"
      "bool GroupCopyFree(PlayerbotAI* ai)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return true;\n")
    string(FIND "${enforce_cpp}" "${fn}" at)
    if(at EQUAL -1)
      set(${out} "SupervisorEnforce.cpp: GroupGoalSnapshot / GroupCopyFree do not start from EnforcingSupervisor(ai)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # The group lane owner is set in one place only (SupervisorGroup.cpp AdoptGroupOwner); ProbeTick keeps its assignment.
  count_regex("${all_sources}" "SetOwner\\(Lane::Group," n)
  if(NOT n EQUAL 1)
    set(${out} "SetOwner(Lane::Group, appears ${n} times in the module (1 expected: Supervisor::AdoptGroupOwner)" PARENT_SCOPE)
    return()
  endif()
  foreach(sig "bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site);"
      "bool GateOffThread(PlayerbotAI* ai, TravelTarget* target, bool nullDest);" "std::uint32_t GroupGoalSnapshot(PlayerbotAI* ai);"
      "bool GroupCopyFree(PlayerbotAI* ai);" "bool BandHeldByGroup(PlayerbotAI* ai);"
      "bool WouldAdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest);" "bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site);")
    string(FIND "${enforce_h}" "${sig}" at)
    if(at EQUAL -1)
      set(${out} "SupervisorEnforce.h: missing ${sig}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (b) the destination generation --------------------------------------------------------------------------------------------
function(check_generation travel_cpp travel_h all_sources out)
  count_regex("${all_sources}" "writeGen_\\.fetch_add\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "writeGen_.fetch_add( appears ${n} times in the module (1 expected: SetTargetUnchecked)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "writeGen_([.{]| *=)" n)
  if(NOT n EQUAL 3)
    set(${out} "writeGen_ is used ${n} times in the module (3 expected: the member, the relaxed read, the one raise; no store)" PARENT_SCOPE)
    return()
  endif()
  region("${travel_cpp}" "void TravelTarget::SetTargetUnchecked(TravelDestination* tDestination1, WorldPosition* wPosition1) {" "\n}\n" unchecked)
  string(FIND "${unchecked}" "writeGen_.fetch_add(1, std::memory_order_relaxed);" raise)
  if(raise EQUAL -1)
    set(${out} "TravelMgr.cpp: the generation is not raised in SetTargetUnchecked" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${unchecked}" "    wPosition = wPosition1;\n    tDestination = tDestination1;\n" assign)
  string(FIND "${unchecked}" "SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);" status_at)
  if(assign EQUAL -1 OR status_at EQUAL -1 OR NOT assign LESS raise OR NOT raise LESS status_at)
    set(${out} "TravelMgr.cpp: the generation is not raised after the destination write and before SetStatus" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${travel_h}" "std::atomic<std::uint32_t> writeGen_{0};" at)
  string(FIND "${travel_h}" "std::uint32_t WriteGen() const { return writeGen_.load(std::memory_order_relaxed); }" at2)
  if(at EQUAL -1 OR at2 EQUAL -1)
    set(${out} "TravelMgr.h: writeGen_ is not an atomic read relaxed by WriteGen()" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (c) every writer call evaluates its result --------------------------------------------------------------------------------
# The per-file census of calls of SetTarget( / setNewTarget( / CopyTarget( (declarations, definitions and the calls of other
# classes included): a new call fails until it is listed here. The two PullStrategy files and MovementActions.cpp call the
# Unit / strategy SetTarget, not the travel target's.
set(CALL_CENSUS
  "QuestBand.cpp=4" "QuestGroupWorld.cpp=2" "strategy/actions/ChooseTravelTargetAction.cpp=8" "strategy/actions/ChooseTravelTargetAction.h=1"
  "strategy/actions/GoAction.cpp=1" "strategy/actions/WorldBuffTravelActions.cpp=1" "strategy/tests/CommandSetup.cpp=1"
  "TravelMgr.cpp=3" "TravelMgr.h=3" "SupervisorEnforce.cpp=3" "SupervisorGroup.cpp=1"
  "strategy/actions/MovementActions.cpp=2" "strategy/generic/PullStrategy.cpp=5" "strategy/generic/PullStrategy.h=1")

# The live-target sites, each with its text, how often, and that it evaluates the result. Local targets (a candidate or a copy
# that is not the bot's live target) are excluded explicitly, with the marker comment.
# Entries "<var>|<count>|<needle>" with "\n" in the needle as the two characters backslash-n (replaced below).
set(LIVE_SITES
  "choose_cpp|1|    if (!setNewTarget(requester, &newTarget, travelTarget))\\n        return false@SEMI@"
  "choose_cpp|2|    if (!setNewTarget(requester, &newTarget, oldTarget))\\n        return false@SEMI@"
  "choose_cpp|1|    if (!target->SetTarget(oldDestination, newPosition))"
  "choose_cpp|1|    if (!oldTarget->CopyTarget(newTarget))\\n        return !oldTarget->TakeArbiterDenied()@SEMI@"
  "band_cpp|1|    if (!setNewTarget(requester, &band, travelTarget))\\n        return false@SEMI@"
  "band_cpp|1|    if (!setNewTarget(requester, &walk, travelTarget))\\n        return false@SEMI@"
  "group_world_cpp|1|    bool const admitted = t->SetTarget(dest, point)@SEMI@\\n    if (!admitted)\\n        return false@SEMI@"
  "group_world_cpp|1|    if (!t->SetTarget(s.dest, s.pos))"
  "go_cpp|1|        if (!target->SetTarget(dest, point))"
  "worldbuff_cpp|1|    if (!travelTarget->SetTarget(dest, point))"
  "setup_cpp|1|        if (!target->SetTarget(tempDest, tempDest->GetPosition()))")
set(LOCAL_SITES
  "band_cpp|1|    band.SetTarget(hub, hub->GetHubPoint())@SEMI@   // local target, not live: no gate"
  "band_cpp|1|    walk.SetTarget(hub, hub->GetHubPoint())@SEMI@   // local target, not live: no gate"
  "choose_cpp|1|                // local target, not live: no gate (A3 step 3 T02-a)\\n                target->SetTarget(destination, position)@SEMI@"
  "choose_cpp|1|        target->SetTarget(campFallbackDestination, campFallbackPosition)@SEMI@   // local target, not live: no gate"
  "travel_cpp|1|        target->SetTarget(nullTravelDestination, nullWorldPosition)@SEMI@")

read_source("QuestBand.cpp" band_cpp)
read_source("QuestGroupWorld.cpp" group_world_cpp)
read_source("strategy/actions/GoAction.cpp" go_cpp)
read_source("strategy/actions/WorldBuffTravelActions.cpp" worldbuff_cpp)
read_source("strategy/tests/CommandSetup.cpp" setup_cpp)

# The texts come from the caller's scope by name (${${var}}); a negative probe swaps one: probe_var / probe_text.
function(check_sites sites probe_var probe_text out)
  foreach(entry IN LISTS sites)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 var)
    list(GET parts 1 want)
    list(GET parts 2 needle)
    string(REPLACE "@SEMI@" ";" needle "${needle}")
    string(REPLACE "\\n" "\n" needle "${needle}")
    if(var STREQUAL probe_var)
      set(text "${probe_text}")
    else()
      set(text "${${var}}")
    endif()
    count_text("${text}" "${needle}" n)
    if(NOT n EQUAL want)
      set(${out} "${var}: ${n} of ${want} expected: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_call_census census out)
  list(SORT census)
  set(expected ${CALL_CENSUS})
  list(SORT expected)
  if(NOT "${census}" STREQUAL "${expected}")
    set(${out} "the SetTarget / setNewTarget / CopyTarget call census changed: now [${census}], expected [${expected}]" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_writers census probe_var probe_text out)
  check_call_census("${census}" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  check_sites("${LIVE_SITES}" "${probe_var}" "${probe_text}" r)
  if(NOT r STREQUAL "")
    set(${out} "live site not evaluated: ${r}" PARENT_SCOPE)
    return()
  endif()
  check_sites("${LOCAL_SITES}" "${probe_var}" "${probe_text}" r)
  if(NOT r STREQUAL "")
    set(${out} "excluded local site changed: ${r}" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (d) LaneFree call sites ---------------------------------------------------------------------------------------------------
# Per file: the calls of LaneFree( (the definition and the declaration of SupervisorEnforce.* included).
set(LANE_FREE_CENSUS
  "strategy/actions/ChooseTravelTargetAction.cpp=1" "strategy/actions/GuildCreateActions.cpp=3"
  "strategy/triggers/StationCraftTrigger.cpp=1" "strategy/values/TravelValues.cpp=1" "SupervisorEnforce.cpp=2" "SupervisorEnforce.h=1")

function(check_lane_free all_sources choose_cpp guild_cpp station_cpp travel_values_cpp enforce_cpp out)
  set(census "")
  file(GLOB_RECURSE files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
  list(SORT files)
  foreach(f IN LISTS files)
    file(READ "${f}" t)
    string(REPLACE "\r" "" t "${t}")
    # A mutated copy of a file under probe replaces the file's text.
    file(RELATIVE_PATH rel "${PB_SOURCE_DIR}" "${f}")
    if(rel STREQUAL "strategy/actions/ChooseTravelTargetAction.cpp")
      set(t "${choose_cpp}")
    elseif(rel STREQUAL "strategy/actions/GuildCreateActions.cpp")
      set(t "${guild_cpp}")
    elseif(rel STREQUAL "strategy/triggers/StationCraftTrigger.cpp")
      set(t "${station_cpp}")
    elseif(rel STREQUAL "strategy/values/TravelValues.cpp")
      set(t "${travel_values_cpp}")
    elseif(rel STREQUAL "SupervisorEnforce.cpp")
      set(t "${enforce_cpp}")
    endif()
    count_regex("${t}" "(^|[^A-Za-z0-9_])LaneFree\\(" n)
    if(n GREATER 0)
      list(APPEND census "${rel}=${n}")
    endif()
  endforeach()
  list(SORT census)
  set(expected ${LANE_FREE_CENSUS})
  list(SORT expected)
  if(NOT "${census}" STREQUAL "${expected}")
    set(${out} "the LaneFree( call sites changed: now [${census}], expected [${expected}]" PARENT_SCOPE)
    return()
  endif()
  # The site names (the counters key on them).
  foreach(pin
      "choose_cpp|ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"request\")"
      "guild_cpp|ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"petition\")"
      "guild_cpp|ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"guild_trip\")"
      "station_cpp|ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"station_craft\")"
      "travel_values_cpp|ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"profession_trip\")"
      "enforce_cpp|LaneFree(ai_, Lane::Questbook, \"probe\")")
    string(REPLACE "|" ";" parts "${pin}")
    list(GET parts 0 var)
    list(GET parts 1 needle)
    string(FIND "${${var}}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "${var}: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_text("${guild_cpp}" "\"guild_trip\")" n)
  if(NOT n EQUAL 2)
    set(${out} "GuildCreateActions.cpp: guild_trip LaneFree ${n} times (2 expected: isUseful and Execute)" PARENT_SCOPE)
    return()
  endif()
  # Request: right after the active-target check in isUseful, before anything that could close the target.
  string(FIND "${choose_cpp}" "    if (AI_VALUE(bool, \"travel target active\"))\n        return false;\n\n    // A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): Execute closes the live target (PREPARE); not while a lane\n    // above the quest book holds it (counted held_request). Named and quest requests inherit this. Always free below 2.\n    if (!ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"request\"))\n        return false;\n" at)
  if(at EQUAL -1)
    set(${out} "ChooseTravelTargetAction.cpp: the request LaneFree is not right after the travel-target-active check of isUseful" PARENT_SCOPE)
    return()
  endif()
  # The four errands carry the Errand writer scope; the lifecycle scope is gone from them.
  foreach(pin
      "guild_cpp|bool PetitionTurnInAction::Execute(Event& event)"
      "guild_cpp|bool RosterGuildFoundTripAction::Execute(Event& event)"
      "station_cpp|bool StationCraftTrigger::IsActive()"
      "travel_values_cpp|void ai::ProfessionTripTick(PlayerbotAI* ai)")
    string(REPLACE "|" ";" parts "${pin}")
    list(GET parts 0 var)
    list(GET parts 1 head)
    region("${${var}}" "${head}" "\n}\n" body)
    string(FIND "${body}" "ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Errand);" at)
    string(FIND "${body}" "WriterId::Lifecycle" lc)
    if(body STREQUAL "" OR at EQUAL -1 OR NOT lc EQUAL -1)
      set(${out} "${var}: ${head} has no Errand writer scope (or still a Lifecycle one)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (e) the closers census ---------------------------------------------------------------------------------------------------
# Every SetStatus(EXPIRED | COOLDOWN | NONE) outside TravelMgr.cpp (the writers that end a target), per file at the base of this
# pair. A new closer needs its place in the arbiter (a lane check) and its entry here.
set(CLOSER_CENSUS
  "PlayerbotAI.cpp=2" "QuestBand.cpp=1" "RandomPlayerbotMgr.cpp=2" "strategy/actions/ChooseTravelTargetAction.cpp=4"
  "strategy/actions/DebugAction.cpp=1" "strategy/actions/FollowActions.cpp=1" "strategy/actions/GuildCreateActions.cpp=2"
  "strategy/actions/MoveToTravelTargetAction.cpp=1" "strategy/actions/ReleaseSpiritAction.h=1"
  "strategy/actions/WorldBuffTravelActions.cpp=3" "strategy/actions/ZoneEscapeActions.cpp=1" "SupervisorGroup.cpp=2"
  "strategy/triggers/StationCraftTrigger.cpp=1" "strategy/values/TravelValues.cpp=1")

function(check_closers census out)
  list(SORT census)
  set(expected ${CLOSER_CENSUS})
  list(SORT expected)
  if(NOT "${census}" STREQUAL "${expected}")
    set(${out} "the SetStatus(EXPIRED|COOLDOWN|NONE) closers outside TravelMgr.cpp changed: now [${census}], expected [${expected}]" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (f) the probe ------------------------------------------------------------------------------------------------------------
function(check_probe enforce_cpp sup_cpp sup_h config_h config_cpp conf_dist all_sources out)
  count_regex("${all_sources}" "sPlayerbotAIConfig\\.supervisorArbiterProbe" n)
  if(NOT n EQUAL 1)
    set(${out} "supervisorArbiterProbe is read in ${n} places (1 expected: the first statement of Supervisor::ProbeTick)" PARENT_SCOPE)
    return()
  endif()
  region("${enforce_cpp}" "void Supervisor::ProbeTick(std::uint32_t nowMs, std::uint32_t arbiter)\n{\n" "\n}\n" probe)
  string(FIND "${probe}" "void Supervisor::ProbeTick(std::uint32_t nowMs, std::uint32_t arbiter)\n{\n    if (sPlayerbotAIConfig.supervisorArbiterProbe != 1 || inProbe_)\n        return;\n" at)
  if(at EQUAL -1)
    set(${out} "SupervisorEnforce.cpp: the first statement of ProbeTick is not the ArbiterProbe key check" PARENT_SCOPE)
    return()
  endif()
  # The probe's writes and the lane it sets exist only inside ProbeTick; ProbeTick is called once, from Tick, behind arbiter >= 1.
  foreach(token "inProbe_ = true;" "owner_ = Lane::Group;")
    count_text("${enforce_cpp}" "${token}" n_all)
    count_text("${probe}" "${token}" n_probe)
    count_text("${sup_cpp}" "${token}" n_sup)
    if(NOT n_all EQUAL 1 OR NOT n_probe EQUAL 1 OR NOT n_sup EQUAL 0)
      set(${out} "SupervisorEnforce.cpp: ${token} must appear once, inside ProbeTick, and not in Supervisor.cpp" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_regex("${all_sources}" "ProbeTick\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "ProbeTick( appears ${n} times in the module (3 expected: declaration, definition, the call in Tick)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${probe}" "[^A-Za-z]target->SetTarget\\(" n)
  count_regex("${enforce_cpp}" "(^|[^A-Za-z0-9_])SetTarget\\(" n_all)
  if(NOT n EQUAL 1 OR NOT n_all EQUAL 2)
    set(${out} "SupervisorEnforce.cpp: the probe writes the live target ${n} times (1 expected) and calls SetTarget( ${n_all} times (2 expected: the live write and the local copy)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${probe}" "copy.SetTarget(dest, pos);   // local target, not live: no gate" at)
  if(at EQUAL -1)
    set(${out} "SupervisorEnforce.cpp: the probe's local copy write lost its marker" PARENT_SCOPE)
    return()
  endif()
  # Only an enforcing supervisor on its own thread writes; at arbiter 1 the probe writes nothing.
  string(FIND "${probe}" "    if (arbiter != 2)\n    {\n        if (DecideAdmit(true, WriterId::Select, false, Lane::Group) == AdmitVerdict::DenyLane)\n            CountEnforce(guid, EnforceCount::ProbeShadow);\n        return;\n    }\n    // The gate acts only on the bot's own map thread (the tick runs there); anywhere else every write would pass.\n    if (!EnforceOn() || !OnOwnThread(ai_))\n        return;\n" at)
  if(at EQUAL -1)
    set(${out} "SupervisorEnforce.cpp: the probe at arbiter 1 is not read-only, or at 2 it runs without enforcement on the own thread" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${sup_cpp}" "    if (arbiter >= 1)\n        ProbeTick(nowMs, arbiter);\n" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.cpp: ProbeTick is not called behind arbiter >= 1 in Tick" PARENT_SCOPE)
    return()
  endif()
  # The key: default 0, clamped to 0 or 1 with an error line, documented once as 0, echoed in its alphabetical place.
  string(FIND "${config_h}" "    uint32 supervisorArbiterProbe = 0;" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.h: supervisorArbiterProbe is not a uint32 member with default 0" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "    supervisorArbiterProbe = uint32(config.GetIntDefault(\"AiPlayerbot.Supervisor.ArbiterProbe\", 0));\n    if (supervisorArbiterProbe > 1)\n    {\n        sLog.outError(\"[Supervisor] config key=Supervisor.ArbiterProbe value=%u result=off (0 off, 1 probe)\", supervisorArbiterProbe);\n        supervisorArbiterProbe = 0;\n    }"
      "            {\"Supervisor.Arbiter\", Value(supervisorArbiter)},\n            {\"Supervisor.ArbiterProbe\", Value(supervisorArbiterProbe)},\n            {\"ThreadSafeTeleportAck\"")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX MATCHALL "\nAiPlayerbot\\.Supervisor\\.ArbiterProbe = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.Supervisor.ArbiterProbe = 0")
    set(${out} "conf.dist: AiPlayerbot.Supervisor.ArbiterProbe not documented once as 0" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "\nAiPlayerbot\\.Supervisor\\.Arbiter = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.Supervisor.Arbiter = 2")
    set(${out} "conf.dist: AiPlayerbot.Supervisor.Arbiter not documented once as 2" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (g) no gate in the destructor ----------------------------------------------------------------------------------------------
function(check_destructor sup_cpp sup_h out)
  region("${sup_cpp}" "Supervisor::~Supervisor()" "\n}\n" dtor)
  if(dtor STREQUAL "")
    set(${out} "Supervisor.cpp: ~Supervisor not found" PARENT_SCOPE)
    return()
  endif()
  foreach(word "AdmitTargetWrite" "LaneFree" "WouldAdmit")
    string(FIND "${dtor}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "Supervisor.cpp: ~Supervisor calls ${word} (the destructor runs on any thread)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # Supervisor.h / .cpp hold no gate call at all: the gate is called by the writers, never by the supervisor.
  foreach(word "AdmitTargetWrite(" "LaneFree(")
    string(FIND "${sup_cpp}" "${word}" a)
    string(FIND "${sup_h}" "${word}" b)
    if(NOT a EQUAL -1 OR NOT b EQUAL -1)
      set(${out} "Supervisor.h/.cpp call ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${dtor}" "ShadowDrop(shadow_);" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.cpp: ~Supervisor no longer only drops the shadow state" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h) the switch values ----------------------------------------------------------------------------------------------------
function(check_values policy config_cpp out)
  string(FIND "${policy}" "inline bool ValidArbiter(std::uint32_t value)\n{\n    return value <= 2;\n}" at)
  if(at EQUAL -1)
    set(${out} "SupervisorPolicy.h: ValidArbiter is not 0, 1 or 2 only" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${policy}" "constexpr int kArbiterEnforce = 2;" at)
  if(at EQUAL -1)
    set(${out} "SupervisorPolicy.h: kArbiterEnforce is not 2" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${config_cpp}" "    if (!ai::supervisor::ValidArbiter(supervisorArbiter))\n    {\n" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.cpp: the Supervisor.Arbiter key is not clamped by ValidArbiter" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the unit test ------------------------------------------------------------------------------------------------------------
function(check_unit unit_test out)
  foreach(needle "void TestArbiterLanes()" "    TestArbiterLanes();" "IsProbeBot")
    string(FIND "${unit_test}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "t/supervisor_policy_tests.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (j) T02-GROUP: who may read the supervisor, and the one group point write ---------------------------------------------------
# Travel code reads the group goal only through the snapshot / filter helpers of SupervisorEnforce.cpp, never ai->GetSupervisor().
# The group handler's one live write (the point at gather flag 1) evaluates its result, like every live site.
read_source("SupervisorGroup.cpp" group_cpp)
function(check_group_regions choose_cpp band_cpp travel_cpp group_cpp out)
  foreach(var choose_cpp band_cpp travel_cpp)
    count_regex("${${var}}" "GetSupervisor" n)
    if(NOT n EQUAL 0)
      set(${out} "${var}: GetSupervisor appears ${n} times (0 expected: travel code uses GroupGoalSnapshot, GroupCopyFree, BandHeldByGroup)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_regex("${group_cpp}" "(^|[^A-Za-z0-9_])SetTarget\\(" n_all)
  count_regex("${group_cpp}" "(if \\(!|[a-z] = )[A-Za-z_]*(->|\\.)SetTarget\\(" n_eval)
  if(NOT n_all EQUAL 1 OR NOT n_eval EQUAL 1)
    set(${out} "SupervisorGroup.cpp: SetTarget( appears ${n_all} times and ${n_eval} of them evaluated (1 and 1 expected: the point write)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- run on the real sources ---------------------------------------------------------------------------------------------------
function(fail_if r)
  if(NOT r STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${r}")
  endif()
endfunction()

check_gate("${travel_cpp}" "${enforce_cpp}" "${enforce_h}" "${all_sources}" r)
fail_if("${r}")
check_generation("${travel_cpp}" "${travel_h}" "${all_sources}" r)
fail_if("${r}")
check_writers("${call_census}" "" "" r)
fail_if("${r}")
check_lane_free("${all_sources}" "${choose_cpp}" "${guild_cpp}" "${station_cpp}" "${travel_values_cpp}" "${enforce_cpp}" r)
fail_if("${r}")
check_closers("${closer_census}" r)
fail_if("${r}")
check_probe("${enforce_cpp}" "${sup_cpp}" "${sup_h}" "${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
fail_if("${r}")
check_destructor("${sup_cpp}" "${sup_h}" r)
fail_if("${r}")
check_values("${policy}" "${config_cpp}" r)
fail_if("${r}")
check_unit("${unit_test}" r)
fail_if("${r}")
check_group_regions("${choose_cpp}" "${band_cpp}" "${travel_cpp}" "${group_cpp}" r)
fail_if("${r}")

# ---- negative probes: each mutation must be caught -----------------------------------------------------------------------------
function(expect_reason result want description)
  string(FIND "${result}" "${want}" at)
  if(result STREQUAL "" OR at EQUAL -1)
    message(FATAL_ERROR "${TAG}: negative probe not caught for its reason (${want}): ${description}; got: ${result}")
  endif()
endfunction()

# (a) a third gate call, a gate missing from either head, a gate in the old body.
check_gate("${travel_cpp}\n    ai::supervisor::AdmitTargetWrite(ai, this, false, \"x\");" "${enforce_cpp}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "AdmitTargetWrite( appears" "a third gate call in TravelMgr.cpp")

string(REPLACE "${SET_TARGET_HEAD}" "bool TravelTarget::SetTarget(TravelDestination* tDestination1, WorldPosition* wPosition1) {\n    SetTargetUnchecked(tDestination1, wPosition1);\n    if (live_ && !ai::supervisor::AdmitTargetWrite(ai, this, IsNullDest(tDestination1), \"set_target\"))\n" m "${travel_cpp}")
check_gate("${m}" "${enforce_cpp}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "first statement of TravelTarget::SetTarget" "a statement before the gate in SetTarget")

string(REPLACE "${COPY_TARGET_HEAD}" "bool TravelTarget::CopyTarget(TravelTarget* const target) {\n    if (live_ && !ai::supervisor::AdmitTargetWrite(ai, this, IsNullDest(target->tDestination), \"copy_target\"))\n" m "${travel_cpp}")
check_gate("${m}" "${enforce_cpp}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "first statement of TravelTarget::CopyTarget" "the arbiterDenied_ reset gone from CopyTarget")

string(REPLACE "    if (live_ && !ai::supervisor::AdmitTargetWrite(ai, this, IsNullDest(tDestination1), \"set_target\"))\n" "    if (!ai::supervisor::AdmitTargetWrite(ai, this, IsNullDest(tDestination1), \"set_target\"))\n" m "${travel_cpp}")
check_gate("${m}" "${enforce_cpp}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "first statement of TravelTarget::SetTarget" "the gate on a target that is not live")

string(REPLACE "    SetTargetUnchecked(target->tDestination, target->wPosition);   // A3 step 3 T02-a: the gate ran at the head" "    SetTargetUnchecked(target->tDestination, target->wPosition);   // A3 step 3 T02-a: the gate ran at the head\n    ai::supervisor::AdmitTargetWrite(ai, this, false, \"x\");" m "${travel_cpp}")
check_gate("${m}" "${enforce_cpp}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "AdmitTargetWrite( appears" "a late gate call in CopyTarget")

string(REPLACE "if (!ai || !ai->GetBot() || !OnOwnThread(ai))\n        return nullptr;\n" "if (!ai || !ai->GetBot())\n        return nullptr;\n" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "own map thread" "the supervisor read on a foreign thread")

check_gate("${travel_cpp}" "${enforce_cpp}\n    ai->GetSupervisor();" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "the supervisor is read in" "a second supervisor read in the gate file")

string(REPLACE "bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return true;" "bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "does not allow at once" "LaneFree without the early allow")

# (b) a second raise, a raise outside the unchecked body, a store.
check_generation("${travel_cpp}" "${travel_h}" "${all_sources}\n    writeGen_.fetch_add(1, std::memory_order_relaxed);" r)
expect_reason("${r}" "writeGen_.fetch_add( appears" "a second generation raise")

check_generation("${travel_cpp}" "${travel_h}" "${all_sources}\n    writeGen_.store(0);" r)
expect_reason("${r}" "writeGen_ is used" "the generation stored")

string(REPLACE "    writeGen_.fetch_add(1, std::memory_order_relaxed);\n" "" m "${travel_cpp}")
string(REPLACE "    SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);" "    writeGen_.fetch_add(1, std::memory_order_relaxed);\n    SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);" m "${m}")
string(REPLACE "    wPosition = wPosition1;\n    tDestination = tDestination1;\n" "    tDestination = tDestination1;\n    wPosition = wPosition1;\n" m "${m}")
check_generation("${m}" "${travel_h}" "${all_sources}" r)
expect_reason("${r}" "after the destination write" "the raise before the destination write")

string(REPLACE "std::atomic<std::uint32_t> writeGen_{0};" "std::uint32_t writeGen_ = 0;" m "${travel_h}")
check_generation("${travel_cpp}" "${m}" "${all_sources}" r)
expect_reason("${r}" "not an atomic" "a plain generation member")

# (c) a new unchecked call, a result thrown away, an excluded line without its marker.
check_writers("${call_census};strategy/actions/ChooseTravelTargetAction.cpp=9" "" "" r)
expect_reason("${r}" "call census changed" "a new SetTarget call")

check_writers("${call_census};NewFile.cpp=1" "" "" r)
expect_reason("${r}" "call census changed" "a new file calling SetTarget")

string(REPLACE "    if (!setNewTarget(requester, &band, travelTarget))\n        return false;" "    setNewTarget(requester, &band, travelTarget);" m "${band_cpp}")
check_writers("${call_census}" "band_cpp" "${m}" r)
expect_reason("${r}" "live site not evaluated" "setNewTarget's result thrown away in the band step")

string(REPLACE "    if (!t->SetTarget(s.dest, s.pos))" "    t->SetTarget(s.dest, s.pos);" m "${group_world_cpp}")
check_writers("${call_census}" "group_world_cpp" "${m}" r)
expect_reason("${r}" "live site not evaluated" "RestoreTravel's SetTarget result ignored")

string(REPLACE "    if (!target->SetTarget(oldDestination, newPosition))" "    target->SetTarget(oldDestination, newPosition);" m "${choose_cpp}")
check_writers("${call_census}" "choose_cpp" "${m}" r)
expect_reason("${r}" "live site not evaluated" "Refresh ignores the SetTarget result")

string(REPLACE "    if (!oldTarget->CopyTarget(newTarget))\n        return !oldTarget->TakeArbiterDenied();" "    if (!oldTarget->CopyTarget(newTarget))\n        return true;" m "${choose_cpp}")
check_writers("${call_census}" "choose_cpp" "${m}" r)
expect_reason("${r}" "live site not evaluated" "a denied copy reported as a success")

string(REPLACE "        if (!target->SetTarget(dest, point))" "        target->SetTarget(dest, point);" m "${go_cpp}")
check_writers("${call_census}" "go_cpp" "${m}" r)
expect_reason("${r}" "live site not evaluated" "the go command ignores the result")

string(REPLACE "    band.SetTarget(hub, hub->GetHubPoint());   // local target, not live: no gate" "    band.SetTarget(hub, hub->GetHubPoint());" m "${band_cpp}")
check_writers("${call_census}" "band_cpp" "${m}" r)
expect_reason("${r}" "excluded local site changed" "a local target line without its marker")

# (d) a LaneFree call removed or added, a wrong site name, the request check moved, a lifecycle scope on an errand.
string(REPLACE "    if (!ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"request\"))\n        return false;\n" "" m "${choose_cpp}")
check_lane_free("${all_sources}" "${m}" "${guild_cpp}" "${station_cpp}" "${travel_values_cpp}" "${enforce_cpp}" r)
expect_reason("${r}" "LaneFree( call sites changed" "the request check removed")

check_lane_free("${all_sources}" "${choose_cpp}\n    ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, \"x\");" "${guild_cpp}" "${station_cpp}" "${travel_values_cpp}" "${enforce_cpp}" r)
expect_reason("${r}" "LaneFree( call sites changed" "a new LaneFree call")

string(REPLACE "\"station_craft\"" "\"station\"" m "${station_cpp}")
check_lane_free("${all_sources}" "${choose_cpp}" "${guild_cpp}" "${m}" "${travel_values_cpp}" "${enforce_cpp}" r)
expect_reason("${r}" "missing" "a renamed errand site")

string(REPLACE "    if (AI_VALUE(bool, \"travel target active\"))\n        return false;\n\n    // A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): Execute closes" "    // A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): Execute closes" m "${choose_cpp}")
check_lane_free("${all_sources}" "${m}" "${guild_cpp}" "${station_cpp}" "${travel_values_cpp}" "${enforce_cpp}" r)
expect_reason("${r}" "right after the travel-target-active check" "the request check detached from the active-target check")

string(REPLACE "    SetTargetUnchecked(tDestination1, wPosition1);\n    if (live_)\n        ai::supervisor::CommitTargetWrite(ai, this);\n" "    SetTargetUnchecked(tDestination1, wPosition1);\n" m "${travel_cpp}")
check_gate("${m}" "${enforce_cpp}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "owner commit is not exactly once" "the public write without the owner commit")

string(REPLACE "    supervisor->StageOwner(OwnerAfter(verdict, writer));" "    supervisor->SetOwner(OwnerAfter(verdict, writer), target->WriteGen() + 1);" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "sets the owner before the write" "the gate committing the owner itself")

string(REPLACE "WriterId::Errand" "WriterId::Lifecycle" m "${station_cpp}")
check_lane_free("${all_sources}" "${choose_cpp}" "${guild_cpp}" "${m}" "${travel_values_cpp}" "${enforce_cpp}" r)
expect_reason("${r}" "no Errand writer scope" "an errand back on the lifecycle scope")

# (e) a new closer, a closer removed.
check_closers("${closer_census};strategy/actions/NewAction.cpp=1" r)
expect_reason("${r}" "closers outside TravelMgr.cpp changed" "a new closer in a new file")

string(REPLACE "strategy/actions/FollowActions.cpp=1" "strategy/actions/FollowActions.cpp=2" m "${closer_census};strategy/actions/FollowActions.cpp=1")
check_closers("${closer_census};strategy/actions/FollowActions.cpp=1" r)
expect_reason("${r}" "closers outside TravelMgr.cpp changed" "a second closer in a known file")

# (f) the probe: key read elsewhere, first statement changed, written at arbiter 1, called without the gate value.
check_probe("${enforce_cpp}" "${sup_cpp}" "${sup_h}" "${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}\n    if (sPlayerbotAIConfig.supervisorArbiterProbe)" r)
expect_reason("${r}" "supervisorArbiterProbe is read in" "the probe key read outside ProbeTick")

string(REPLACE "    if (sPlayerbotAIConfig.supervisorArbiterProbe != 1 || inProbe_)\n        return;\n" "    Player* const early = ai_ ? ai_->GetBot() : nullptr;\n    if (sPlayerbotAIConfig.supervisorArbiterProbe != 1 || inProbe_)\n        return;\n" m "${enforce_cpp}")
check_probe("${m}" "${sup_cpp}" "${sup_h}" "${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "first statement of ProbeTick" "work before the key check")

string(REPLACE "    if (arbiter != 2)\n    {\n        if (DecideAdmit(true, WriterId::Select, false, Lane::Group) == AdmitVerdict::DenyLane)\n            CountEnforce(guid, EnforceCount::ProbeShadow);\n        return;\n    }\n" "    if (arbiter != 2)\n    {\n        target->SetTarget(dest, pos);\n        return;\n    }\n" m "${enforce_cpp}")
check_probe("${m}" "${sup_cpp}" "${sup_h}" "${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "the probe writes the live target" "the probe writes at arbiter 1")

string(REPLACE "    if (!EnforceOn() || !OnOwnThread(ai_))\n        return;\n" "" m "${enforce_cpp}")
check_probe("${m}" "${sup_cpp}" "${sup_h}" "${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "arbiter 1 is not read-only" "the probe without enforcement and the own thread")

string(REPLACE "    if (arbiter >= 1)\n        ProbeTick(nowMs, arbiter);\n" "    ProbeTick(nowMs, arbiter);\n" m "${sup_cpp}")
check_probe("${enforce_cpp}" "${m}" "${sup_h}" "${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "behind arbiter >= 1" "the probe called without the arbiter value")

string(REPLACE "    uint32 supervisorArbiterProbe = 0;" "    uint32 supervisorArbiterProbe = 1;" m "${config_h}")
check_probe("${enforce_cpp}" "${sup_cpp}" "${sup_h}" "${m}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "default 0" "the probe member default 1")

string(REPLACE "AiPlayerbot.Supervisor.ArbiterProbe = 0" "AiPlayerbot.Supervisor.ArbiterProbe = 1" m "${conf_dist}")
check_probe("${enforce_cpp}" "${sup_cpp}" "${sup_h}" "${config_h}" "${config_cpp}" "${m}" "${all_sources}" r)
expect_reason("${r}" "not documented once as 0" "the probe documented as 1")

string(REPLACE "            {\"Supervisor.ArbiterProbe\", Value(supervisorArbiterProbe)},\n" "" m "${config_cpp}")
check_probe("${enforce_cpp}" "${sup_cpp}" "${sup_h}" "${config_h}" "${m}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "missing" "the probe key not echoed")

# (j) T02-GROUP: the gate-off counter, the snapshot entry points, the one group owner write, the one point write, the census.
set(ADMIT_HEAD_NEW "bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return GateOffThread(ai, target, nullDest);\n    if (!target || !target->IsLive())\n        return true;")
string(REPLACE "${ADMIT_HEAD_NEW}" "bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor || !target || !target->IsLive())\n        return true;" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "does not allow at once" "the old AdmitTargetWrite head without GateOffThread")

string(REPLACE "${ADMIT_HEAD_NEW}" "bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site)\n{\n    GateOffThread(ai, target, nullDest);\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return true;\n    if (!target || !target->IsLive())\n        return true;" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "does not allow at once" "GateOffThread before EnforcingSupervisor")

string(REPLACE "bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return true;" "bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n    if (!supervisor)\n        return true;\n    GateOffThread(ai, nullptr, false);" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "GateOffThread( must be defined and called once" "GateOffThread called from LaneFree")

string(REPLACE "bool GateOffThread(PlayerbotAI* ai, TravelTarget* target, bool nullDest)\n{\n" "bool GateOffThread(PlayerbotAI* ai, TravelTarget* target, bool nullDest)\n{\n    int GetSupervisorFlag = 0;\n" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "GateOffThread reads the supervisor" "a supervisor read in GateOffThread")

string(REPLACE "    if (ArbiterValue(sPlayerbotAIConfig.supervisorArbiter) != 2)\n        return true;\n" "" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "GateOffThread reads the supervisor or does not start" "GateOffThread without the arbiter != 2 early return")

string(REPLACE "std::uint32_t GroupGoalSnapshot(PlayerbotAI* ai)\n{\n    Supervisor* const supervisor = EnforcingSupervisor(ai);\n" "std::uint32_t GroupGoalSnapshot(PlayerbotAI* ai)\n{\n    Supervisor* const supervisor = nullptr;\n" m "${enforce_cpp}")
check_gate("${travel_cpp}" "${m}" "${enforce_h}" "${all_sources}" r)
expect_reason("${r}" "do not start from EnforcingSupervisor" "the snapshot without EnforcingSupervisor")

check_gate("${travel_cpp}" "${enforce_cpp}" "${enforce_h}" "${all_sources}\n    SetOwner(Lane::Group, 1);" r)
expect_reason("${r}" "SetOwner(Lane::Group, appears" "a second group owner write")

check_group_regions("${choose_cpp}\n    ai->GetSupervisor();" "${band_cpp}" "${travel_cpp}" "${group_cpp}" r)
expect_reason("${r}" "choose_cpp: GetSupervisor" "the supervisor read in the travel target action")
check_group_regions("${choose_cpp}" "${band_cpp}\n    ai->GetSupervisor();" "${travel_cpp}" "${group_cpp}" r)
expect_reason("${r}" "band_cpp: GetSupervisor" "the supervisor read in QuestBand.cpp")
check_group_regions("${choose_cpp}" "${band_cpp}" "${travel_cpp}\n    ai->GetSupervisor();" "${group_cpp}" r)
expect_reason("${r}" "travel_cpp: GetSupervisor" "the supervisor read in TravelMgr.cpp")
check_group_regions("${choose_cpp}" "${band_cpp}" "${travel_cpp}" "${group_cpp}\n    target->SetTarget(dest, pos);" r)
expect_reason("${r}" "SetTarget( appears" "a second group write")
string(REGEX REPLACE "(if \\(!|[a-z] = )([A-Za-z_]*(->|\\.)SetTarget\\()" "\\2" m "${group_cpp}")
check_group_regions("${choose_cpp}" "${band_cpp}" "${travel_cpp}" "${m}" r)
expect_reason("${r}" "evaluated" "the point write result thrown away")

check_writers("${call_census};SupervisorGroup.cpp=2" "" "" r)
expect_reason("${r}" "call census changed" "a second SetTarget call in SupervisorGroup.cpp")
check_closers("${closer_census};SupervisorGroup.cpp=1" r)
expect_reason("${r}" "closers outside TravelMgr.cpp changed" "a third closer in SupervisorGroup.cpp")

# (g) a gate call in the destructor or in the supervisor files.
string(REPLACE "    ShadowDrop(shadow_);\n    UnregisterMail(mailGuid_, mailToken_);\n}" "    ShadowDrop(shadow_);\n    UnregisterMail(mailGuid_, mailToken_);\n    LaneFree(ai_, Lane::Questbook, \"x\");\n}" m "${sup_cpp}")
check_destructor("${m}" "${sup_h}" r)
expect_reason("${r}" "~Supervisor calls" "a gate call in ~Supervisor")

check_destructor("${sup_cpp}\n    AdmitTargetWrite(ai_, nullptr, false, \"x\");" "${sup_h}" r)
expect_reason("${r}" "call AdmitTargetWrite(" "a gate call elsewhere in Supervisor.cpp")

# (h) the switch values: 3 let through, the clamp gone.
string(REPLACE "    return value <= 2;" "    return value <= 3;" m "${policy}")
check_values("${m}" "${config_cpp}" r)
expect_reason("${r}" "0, 1 or 2 only" "a value above 2 let through")

string(REPLACE "    if (!ai::supervisor::ValidArbiter(supervisorArbiter))\n    {\n" "    if (false)\n    {\n" m "${config_cpp}")
check_values("${policy}" "${m}" r)
expect_reason("${r}" "not clamped" "no clamp")

# the unit test
string(REPLACE "    TestArbiterLanes();\n" "" m "${unit_test}")
check_unit("${m}" r)
expect_reason("${r}" "TestArbiterLanes" "the lane unit test not run")

message(STATUS "${TAG}: OK")
