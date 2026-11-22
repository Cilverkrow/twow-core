cmake_policy(VERSION 3.16)
# A3 R9 / R2 wiring pair R9-2 (twow-repo#575; R9-REPLAN-INTERFACE v5 9575095d, R9-PREREG C1/C2; DESIGN-R9-2-20261120.md D1-D13;
# AiPlayerbot.Replan / AiPlayerbot.Replan.Shadow, default 0): the progress meter (ReplanPolicy.h) in the planner tick. Pinned here:
#   (a) the keys: members 0, read with default 0, Replan above 3 and Replan.Shadow above 1 off with an error line, echoed in byte
#       order, documented once = 0 with the Supervisor / Planner requirement and the cohort mode;
#   (b) the tick: with both keys 0 the P-2b take every tick and the relocate counting stay (D3), the meter is made in one place and
#       dropped while off, ReplanTick runs after the W8 block and before the plan gate, relocation before Feed (v5 7.1), the
#       scope (D4), kills 0 (D10), the credit from the core status by find (D9), the RelocationEvent is never planned (no route
#       executor), the [Plan] line names the source by ai::replan::SourceName;
#   (c) lines and counters only (D1): no planner memory write, no event bit, no verdict application in ReplanTick;
#   (d) the [Replan] budget: its own 300 per minute (D11), every forced record outside it (ReplanWirePolicy.h OutsideLineBudget),
#       the minute counters held per bot and flushed under the lock once per minute or with a decided or refused line; R9-a (OB-10
#       review core#1100): forced lines under their own global ceiling (ReplanWirePolicy.h kForcedLinesPerMinute = 600), a refused
#       one counted as forced_dropped= of its cohort;
#   (e) the sink: Supervisor is the IReplanSink, its OnStall / OnRelocate bodies live in PlannerShadow.cpp (not Supervisor.cpp);
#   (f) the latch: the placeholder enum of P-2b is gone, PlayerbotAI holds one atomic number, every writer returns at once while
#       both keys are 0, the ack is noted on both paths of HandleTeleportAck, the unreachable count only while wired;
#   (g) the sites (D6, D12): hearth, home teleport (stuck combat, pos stuck: only a started teleport), rescue (only a started
#       teleport), far-move arrival
#       and the hop marks; no site sets Death, Revive or TeleportAck (those come from the snapshot edges and the ack);
#   (h) ReplanWirePolicy.h is pure, and tests.cmake registers replan_wire_policy after replan_policy;
#   (i) the OB-10 conditions of the design review (20.11.): R-2 the dead hold before the relocation and Feed, R-3 the acked hop
#       mark dropped at a tick without a jump and hops= counted, R-5 a grind target not fed (grind_nokill=), R-6 offmap= tagged and
#       counted only when the meter idled for it, R-7 (latch writers) = (ReplanWired() gates) = 3. The [Plan] golden of R-7 is text
#       level only here (the D3 off path above, and the full P-2b [Plan] line with the off-path source in
#       t/replan_wire_policy_tests.cpp); the runtime golden ([Plan] / [PlanMin] at Planner 1, both R9 keys 0, against P-2b) is owed.
# Every check_* returns "" when it holds, else the reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "r9 replan wiring: PB_SOURCE_DIR is required")
endif()
get_filename_component(PB_MODULE_ROOT "${PB_SOURCE_DIR}/../.." ABSOLUTE)

set(TAG "r9 replan wiring")

function(read_file path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(fail_if reason)
  if(NOT "${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${reason}")
  endif()
endfunction()

# Number of non-overlapping occurrences of needle (plain text, no regex, no list splitting).
function(count_text text needle out_var)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" nl)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR cut "${at} + ${nl}")
    string(SUBSTRING "${rest}" ${cut} -1 rest)
  endwhile()
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

# The text from `head` up to the first closing brace at column 0 ("" when head is missing).
function(body_of text head out_var)
  string(FIND "${text}" "${head}" at)
  if(at EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    set(${out_var} "${rest}" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${end} rest)
  set(${out_var} "${rest}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf)
read_file("${PB_SOURCE_DIR}/PlannerShadow.cpp" shadow_cpp)
read_file("${PB_SOURCE_DIR}/PlannerShadow.h" shadow_h)
read_file("${PB_SOURCE_DIR}/Supervisor.h" sup_h)
read_file("${PB_SOURCE_DIR}/Supervisor.cpp" sup_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_file("${PB_SOURCE_DIR}/ReplanWirePolicy.h" wire)
read_file("${PB_SOURCE_DIR}/strategy/actions/UseItemAction.cpp" useitem)
read_file("${PB_SOURCE_DIR}/strategy/actions/MovementActions.cpp" movement)
read_file("${PB_SOURCE_DIR}/TravelMgr.cpp" travelmgr)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" rndmgr)
read_file("${PB_MODULE_ROOT}/tests.cmake" tests)

# need_once(name text out needle...): every needle exactly once in text; else the reason. The needles are read from ARGV<n>, so a
# needle with a semicolon stays one needle.
function(need_once name text out)
  math(EXPR last "${ARGC} - 1")
  if(last LESS 3)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  foreach(i RANGE 3 ${last})
    set(needle "${ARGV${i}}")
    count_text("${text}" "${needle}" n)
    if(NOT n EQUAL 1)
      set(${out} "${name}: '${needle}' found ${n} times, want 1" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# need_none(name text out needle...): no needle in text; else the reason.
function(need_none name text out)
  math(EXPR last "${ARGC} - 1")
  if(last LESS 3)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  foreach(i RANGE 3 ${last})
    set(needle "${ARGV${i}}")
    string(FIND "${text}" "${needle}" at)
    if(NOT at EQUAL -1)
      set(${out} "${name}: names '${needle}'" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (a) the keys -----------------------------------------------------------------------------------------------------------
function(check_keys config_h config_cpp conf out)
  need_once("PlayerbotAIConfig.h" "${config_h}" r "    uint32 replan = 0;\n    uint32 replanShadow = 0;\n")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("PlayerbotAIConfig.cpp" "${config_cpp}" r
    "    replan = uint32(config.GetIntDefault(\"AiPlayerbot.Replan\", 0));\n    if (replan > 3)\n    {\n        sLog.outError(\"[Replan] config key=Replan value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", replan);\n        replan = 0;\n    }\n"
    "    replanShadow = uint32(config.GetIntDefault(\"AiPlayerbot.Replan.Shadow\", 0));\n    if (replanShadow > 1)\n    {\n        sLog.outError(\"[Replan] config key=Replan.Shadow value=%u result=off (0 off, 1 shadow)\", replanShadow);\n        replanShadow = 0;\n    }\n"
    "            {\"QuestTurnIn.RepeatableAutoComplete\", Value(questTurnInRepeatableAutoComplete)},\n            {\"Replan\", Value(replan)},\n            {\"Replan.Shadow\", Value(replanShadow)},\n            {\"Rescue.RepopDeadOnly\", Value(rescueRepopDeadOnly)},\n")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("aiplayerbot.conf.dist.in" "${conf}" r
    "\nAiPlayerbot.Replan = 0\n"
    "\nAiPlayerbot.Replan.Shadow = 0\n"
    "both keys need AiPlayerbot.Supervisor to apply to the bot and AiPlayerbot.Planner >= 1"
    "# AiPlayerbot.Replan is a cohort mode (CohortPolicy.h)")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "\nAiPlayerbot\\.Replan(\\.Shadow)? =" lines "${conf}")
  list(LENGTH lines n)
  if(NOT n EQUAL 2)
    set(${out} "aiplayerbot.conf.dist.in: ${n} Replan key lines, want 2" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (b) the tick, (c) lines and counters only, (d) the budget --------------------------------------------------------------
set(OFF_PATH "    bool const wired = ai_->ReplanWired();
    bool const jumped = (ev & (EvRelocate | EvMap)) != 0;
    ai::replan_wire::Taken taken;
    if (!wired || jumped)
        taken = ai_->TakeLastRelocSource(nowMs);
    else
        ai_->ExpireLastRelocSource(nowMs);
    RelocSource relocSrc = taken.source;
    if (ev & EvRelocate)
    {
        ++c.relocate;
        if (relocSrc == RelocSource::Unknown)
            ++c.relocUnknown;
        dirty = true;
    }
    else
        relocSrc = RelocSource::Unknown;
")
set(TICK_CALL "    if (wired)
        ReplanTick(ai_, bot, st, cur, target, ev, taken, guid, nowMs, arbiter);
    else
        st.replan.reset();
")

function(check_tick shadow_cpp out)
  need_once("PlannerShadow.cpp" "${shadow_cpp}" r
    "${OFF_PATH}" "${TICK_CALL}" "TakeLastRelocSource(" "ExpireLastRelocSource(" "std::make_unique<ReplanState>()"
    "void ReplanTick(" "ReplanTick(ai_," ".OnRelocate(e, *sink)" ".Feed(s, *sink)"
    "    bool const scope = ai_->InQuestBookScope() && !bot->InBattleGround() && !ai_->IsParked();"
    "    s.kills = 0;" "statuses.find(questId)" "        e.planned = false;" "        e.legIndex = ai::replan::NoLeg;"
    "l.relocSrc = (ev & EvRelocate) ? ai::replan::SourceName(relocSrc) : \"-\";"
    "constexpr std::uint32_t kReplanLinesPerMinute = 300;"
    "    return ai::rot_cast::BudgetTake(g_rWindow, g_rUsed, std::uint32_t(time(nullptr)), kReplanLinesPerMinute);"
    "    if (ai::replan_wire::OutsideLineBudget(r))\n    {\n        if (!TakeForcedLine())\n        {\n            ++forcedDropped;\n            return;\n        }\n    }\n    else if (!TakeReplanLine())\n    {\n        ++dropped;\n        return;\n    }\n"
    "    return ai::rot_cast::BudgetTake(g_fWindow, g_fUsed, std::uint32_t(time(nullptr)), ai::replan_wire::kForcedLinesPerMinute);"
    "    ai::replan::Counters k = rs.meter.TakeCounters();\n    k.forcedDropped += forcedDropped;\n"
    "ev |= EvClosed;"
    "    bool const grindNoKill = s.targetKey && ai::replan_wire::GrindNoKill(s.kind);\n    if (grindNoKill)\n        s.targetKey = 0;\n"
    "    ai::replan_wire::HoldWhileDead(s, rs.held, cur.alive);\n"
    "    ctx.offmap = ai::replan_wire::OffMap(s.targetKey, s.remaining);"
    "        ++k.hops;" "    if (grindNoKill && !s.targetKey)\n        ++k.grindNoKill;" "        k.offMap += decided;"
    "    ai::replan::Add(rs.pending, k);\n    rs.pendingDropped += dropped;\n"
    "    if (decided || dropped || minute != rs.minute)\n    {\n        rs.minute = minute;\n        FlushReplan(guid, rs.pending, rs.pendingDropped, ctx.shadow);\n")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_none("PlannerShadow.cpp" "${shadow_cpp}" r "RelocSourceName" "ApplyReplanVerdict" "CloseReason::R9" "EmitEnforceLine"
    "getQuestStatusMap()[")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  count_text("${shadow_cpp}" "kReplanLinesPerMinute" n)
  if(NOT n EQUAL 2)
    set(${out} "PlannerShadow.cpp: kReplanLinesPerMinute ${n} times, want 2 (the constant and the [Replan] budget)" PARENT_SCOPE)
    return()
  endif()
  # order: the W8 block, the meter, the plan gate; relocation before Feed
  string(FIND "${shadow_cpp}" "ev |= EvClosed;" at_w8)
  string(FIND "${shadow_cpp}" "${TICK_CALL}" at_call)
  string(FIND "${shadow_cpp}" "    bool const due =" at_due)
  if(NOT (at_w8 LESS at_call AND at_call LESS at_due))
    set(${out} "PlannerShadow.cpp: ReplanTick must run after the W8 block and before the plan gate" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${shadow_cpp}" ".OnRelocate(e, *sink)" at_reloc)
  string(FIND "${shadow_cpp}" ".Feed(s, *sink)" at_feed)
  if(NOT at_reloc LESS at_feed)
    set(${out} "PlannerShadow.cpp: the relocation must reach the meter before Feed (v5 7.1)" PARENT_SCOPE)
    return()
  endif()
  # OB-10 R-5 / R-2: the grind skip and the dead hold shape the sample before the relocation and Feed see it
  string(FIND "${shadow_cpp}" "ai::replan_wire::GrindNoKill(s.kind)" at_grind)
  string(FIND "${shadow_cpp}" "ai::replan_wire::HoldWhileDead(s, rs.held, cur.alive);" at_hold)
  if(NOT (at_grind LESS at_hold AND at_hold LESS at_reloc))
    set(${out} "PlannerShadow.cpp: the grind skip, then the dead hold, must come before the relocation and Feed" PARENT_SCOPE)
    return()
  endif()
  # (c) D1: the meter's body reads the planner state and writes only its own state and lines
  body_of("${shadow_cpp}" "void ReplanTick(" body)
  need_none("ReplanTick" "${body}" r "ev |=" "st.memory" "st.pick =" "st.pick.has =" "Close(" "dirty = true" "NoteChoice("
    "->Set" "st.lastPlanMs" "st.deadWaitSinceMs =")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # (d) the cohort lock once per minute per bot, not once per tick: two flushes (the minute or a line, and before the meter is
  # dropped), both of the held counts
  count_text("${body}" "FlushReplan(" n)
  count_text("${body}" "FlushReplan(guid, k," per_tick)
  if(NOT n EQUAL 2 OR NOT per_tick EQUAL 0)
    set(${out} "ReplanTick: ${n} FlushReplan calls (want 2), ${per_tick} of the tick's own counts (want 0: held per bot)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (e) the sink ------------------------------------------------------------------------------------------------------------
function(check_sink sup_h sup_cpp shadow_cpp out)
  need_once("Supervisor.h" "${sup_h}" r "class Supervisor : public ai::replan::IReplanSink"
    "    ai::replan::Verdict OnStall(ai::replan::StallEvent const& e) override;"
    "    ai::replan::Verdict OnRelocate(ai::replan::RelocationEvent const& e) override;")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("PlannerShadow.cpp" "${shadow_cpp}" r "ai::replan::Verdict Supervisor::OnStall(" "ai::replan::Verdict Supervisor::OnRelocate(")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_none("Supervisor.cpp" "${sup_cpp}" r "Supervisor::OnStall(" "Supervisor::OnRelocate(")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (f) the latch -----------------------------------------------------------------------------------------------------------
function(check_latch shadow_h ai_h ai_cpp out)
  need_none("PlannerShadow.h" "${shadow_h}" r "enum class RelocSource" "RelocSourceName")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("PlannerShadow.h" "${shadow_h}" r "    std::unique_ptr<ReplanState> replan;")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("PlayerbotAI.h" "${ai_h}" r "std::atomic<std::uint64_t> lastRelocSource_{0};" "    bool ReplanWired() const;"
    "    ai::replan_wire::Taken TakeLastRelocSource(std::uint32_t nowMs);")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("PlayerbotAI.cpp" "${ai_cpp}" r
    "    return sPlayerbotAIConfig.replan || sPlayerbotAIConfig.replanShadow;\n"
    "void PlayerbotAI::SetLastRelocSource(ai::replan::RelocSource source)\n{\n    if (!ReplanWired())\n        return;\n"
    "void PlayerbotAI::MarkRelocHop()\n{\n    if (!ReplanWired())\n        return;\n"
    "void PlayerbotAI::NoteTeleportAck()\n{\n    if (!ReplanWired())\n        return;\n"
    "    if (ReplanWired())\n        ++replanUnreachable_;"
    "lastRelocSource_.exchange(0, std::memory_order_acq_rel)"
    "    if (ai::replan_wire::DropAtTickEnd(cur, nowMs))")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # OB-10 R-7: every latch writer is gated: (writers that merge into the latch) = (ReplanWired() early returns) = 3
  count_text("${ai_cpp}" "MergeRelocLatch(lastRelocSource_," writers)
  count_text("${ai_cpp}" "\n{\n    if (!ReplanWired())\n        return;\n" gates)
  if(NOT writers EQUAL 3 OR NOT gates EQUAL writers)
    set(${out} "PlayerbotAI.cpp: ${writers} latch writers, ${gates} ReplanWired() gates (want 3 and 3, OB-10 R-7)" PARENT_SCOPE)
    return()
  endif()
  body_of("${ai_cpp}" "void PlayerbotAI::HandleTeleportAck()" ack)
  count_text("${ack}" "NoteTeleportAck();" n)
  if(NOT n EQUAL 2)
    set(${out} "PlayerbotAI.cpp: HandleTeleportAck notes ${n} acks, want 2 (near and far)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (g) the sites -----------------------------------------------------------------------------------------------------------
function(check_sites ai_cpp useitem movement travelmgr rndmgr out)
  need_once("UseItemAction.cpp" "${useitem}" r "ai->SetLastRelocSource(ai::replan::RelocSource::Hearth);")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("PlayerbotAI.cpp" "${ai_cpp}" r
    "                    bool const homed = bot->TeleportToHomebind(0, true);\n                    if (homed)\n                        SetLastRelocSource(ai::replan::RelocSource::HomeTeleport);")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("TravelMgr.cpp" "${travelmgr}" r
    "    if (moved == pos_stuck::MoveResult::Moved && bot->IsBeingTeleported())\n        ai->SetLastRelocSource(ai::replan::RelocSource::HomeTeleport);")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("RandomPlayerbotMgr.cpp" "${rndmgr}" r "bool const rescued = bot->TeleportTo("
    "        if (rescued)\n            ai->SetLastRelocSource(ai::replan::RelocSource::Rescue);")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("MovementActions.cpp" "${movement}" r
    "RelocSiteMark const arrivalMark(ai, bot, false, ai::replan::RelocSource::FarMoveArrival);"
    "RelocSiteMark const hopMark(ai, bot, true, ai::replan::RelocSource::Unknown);"
    "        if (!wired_ || was_ || !bot_->IsBeingTeleported())")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  count_text("${movement}" "ai->MarkRelocHop();" n)
  if(NOT n EQUAL 4)
    set(${out} "MovementActions.cpp: ${n} direct hop marks, want 4 (fly direct, two minimal-move steps, legacy MoveTo)" PARENT_SCOPE)
    return()
  endif()
  foreach(pair "PlayerbotAI.cpp|${ai_cpp}" "UseItemAction.cpp|${useitem}" "MovementActions.cpp|${movement}"
               "TravelMgr.cpp|${travelmgr}" "RandomPlayerbotMgr.cpp|${rndmgr}")
    string(FIND "${pair}" "|" bar)
    string(SUBSTRING "${pair}" 0 ${bar} name)
    math(EXPR from "${bar} + 1")
    string(SUBSTRING "${pair}" ${from} -1 text)
    need_none("${name}" "${text}" r "RelocSource::Death" "RelocSource::Revive" "RelocSource::TeleportAck")
    if(r)
      set(${out} "${r} (D5/D7: death, revive and the ack are no site labels)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h) pure rules and registration -----------------------------------------------------------------------------------------
function(check_wire wire tests out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${wire}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include (<(cmath|cstdint)>|\"(ReplanPolicy|CohortPolicy)\\.h\")$")
      set(${out} "ReplanWirePolicy.h: include '${inc}' (pure rules)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  need_none("ReplanWirePolicy.h" "${wire}" r "ProgressMeter" "Player*" "Player&" "Unit*" "sLog" "sPlayerbotAIConfig" "getMSTime"
    "std::atomic" "std::mutex" "GetIntDefault" "static std::" "thread_local")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # D11: every forced record is outside the [Replan] budget (unit suite: ForcedOutsideBudget)
  need_once("ReplanWirePolicy.h" "${wire}" r "inline bool OutsideLineBudget(ai::replan::Record const& r)\n{\n    return r.forced;\n}\n")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # R9-a: the global forced-line ceiling (unit suite: ForcedLineCeiling)
  need_once("ReplanWirePolicy.h" "${wire}" r "constexpr std::uint32_t kForcedLinesPerMinute = 600;")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  need_once("tests.cmake" "${tests}" r "add_test(NAME replan_wire_policy\n  COMMAND replan_wire_policy_tests")
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${tests}" "add_test(NAME replan_policy\n" at_pol)
  string(FIND "${tests}" "add_executable(replan_wire_policy_tests" at_wire)
  if(at_pol EQUAL -1 OR NOT at_pol LESS at_wire)
    set(${out} "tests.cmake: replan_wire_policy must follow the replan_policy block" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

check_keys("${config_h}" "${config_cpp}" "${conf}" r)
fail_if("${r}")
check_tick("${shadow_cpp}" r)
fail_if("${r}")
check_sink("${sup_h}" "${sup_cpp}" "${shadow_cpp}" r)
fail_if("${r}")
check_latch("${shadow_h}" "${ai_h}" "${ai_cpp}" r)
fail_if("${r}")
check_sites("${ai_cpp}" "${useitem}" "${movement}" "${travelmgr}" "${rndmgr}" r)
fail_if("${r}")
check_wire("${wire}" "${tests}" r)
fail_if("${r}")

# ---- negative probes ---------------------------------------------------------------------------------------------------------
set(probes 0)
macro(expect_caught what reason)
  if("${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
  math(EXPR probes "${probes} + 1")
endmacro()

string(REPLACE "    if (replan > 3)\n" "    if (replan > 4)\n" m "${config_cpp}")
check_keys("${config_h}" "${m}" "${conf}" r)
expect_caught("Replan clamp widened" "${r}")
string(REPLACE "\nAiPlayerbot.Replan.Shadow = 0\n" "\nAiPlayerbot.Replan.Shadow = 1\n" m "${conf}")
check_keys("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("Replan.Shadow documented on" "${r}")
string(REPLACE "            {\"Replan\", Value(replan)},\n" "" m "${config_cpp}")
check_keys("${config_h}" "${m}" "${conf}" r)
expect_caught("Replan not echoed" "${r}")

# D3: the P-2b take every tick must stay on the off path
string(REPLACE "    if (!wired || jumped)\n        taken = ai_->TakeLastRelocSource(nowMs);" "    if (jumped)\n        taken = ai_->TakeLastRelocSource(nowMs);" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("the off path no longer takes every tick (D3)" "${r}")
string(REPLACE "            ++c.relocUnknown;\n" "" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("the relocate counting changed (D3)" "${r}")
string(REPLACE "    else\n        st.replan.reset();\n" "\n" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("the meter kept while off" "${r}")
string(REPLACE "    if (!st.replan)\n        st.replan = std::make_unique<ReplanState>();" "    st.replan = std::make_unique<ReplanState>();\n    if (!st.replan)\n        st.replan = std::make_unique<ReplanState>();" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("a second make site" "${r}")
string(REPLACE "    ai::replan::Outcome const fed = rs.meter.Feed(s, *sink);\n" "" m "${shadow_cpp}")
string(REPLACE "    RelocSource source = RelocSource::Unknown;\n" "    ai::replan::Outcome const fed = rs.meter.Feed(s, *sink);\n    RelocSource source = RelocSource::Unknown;\n" m "${m}")
check_tick("${m}" r)
expect_caught("Feed before the relocation" "${r}")
string(REPLACE "    ai::replan_wire::HoldWhileDead(s, rs.held, cur.alive);\n" "" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("no dead hold: the ghost run credits (OB-10 R-2)" "${r}")
string(REPLACE "    if (grindNoKill)\n        s.targetKey = 0;\n" "" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("a grind target fed without kills (OB-10 R-5)" "${r}")
string(REPLACE "    ai::replan::Counters k = rs.meter.TakeCounters();" "    ev |= EvClosed;\n    ai::replan::Counters k = rs.meter.TakeCounters();" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("an event bit from the meter (D1)" "${r}")
string(REPLACE "    ai::replan::Counters k = rs.meter.TakeCounters();" "    Close(st.memory, kMemorySlots, rs.held.targetKey, 0, 0, CloseReason::Dead, nowMs, 1u);\n    ai::replan::Counters k = rs.meter.TakeCounters();" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("a planner memory write from the meter (D1)" "${r}")
string(REPLACE "kReplanLinesPerMinute = 300;" "kReplanLinesPerMinute = 600;" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("the [Replan] budget changed" "${r}")
string(REPLACE "TakeReplanLine()" "TakePlanLine()" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("[Replan] on the [Plan] budget" "${r}")
string(REPLACE "    if (ai::replan_wire::OutsideLineBudget(r))\n    {\n        if (!TakeForcedLine())\n        {\n            ++forcedDropped;\n            return;\n        }\n    }\n    else if (!TakeReplanLine())" "    if (!ai::replan_wire::OutsideLineBudget(r) && !TakeReplanLine())" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("forced lines without a ceiling (R9-a)" "${r}")
string(REPLACE "        if (!TakeForcedLine())" "        if (!TakeReplanLine())" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("a forced line on the [Replan] budget (R9-a)" "${r}")
string(REPLACE "ai::replan_wire::kForcedLinesPerMinute);" "kReplanLinesPerMinute);" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("the forced ceiling at the [Replan] budget (R9-a)" "${r}")
string(REPLACE "    k.forcedDropped += forcedDropped;\n" "" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("forced_dropped not counted (R9-a)" "${r}")
string(REPLACE "FlushReplan(guid, rs.pending, rs.pendingDropped, ctx.shadow);" "FlushReplan(guid, k, dropped, ctx.shadow);" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("the cohort lock taken every tick" "${r}")
string(REPLACE "    if (grindNoKill && !s.targetKey)\n" "    if (grindNoKill)\n" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("grind_nokill counted while the dead hold feeds the meter" "${r}")
string(REPLACE "    s.kills = 0;" "    s.kills = KillsOf(bot);" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("a kill source without a hook (D10)" "${r}")
string(REPLACE "        e.planned = false;" "        e.planned = true;" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("a planned relocation without a route executor" "${r}")
string(REPLACE "ai::replan::SourceName(relocSrc)" "RelocSourceName(relocSrc)" m "${shadow_cpp}")
check_tick("${m}" r)
expect_caught("the placeholder name at the [Plan] line" "${r}")

string(REPLACE "ai::replan::Verdict Supervisor::OnStall(" "ai::replan::Verdict SupervisorOnStall(" m "${shadow_cpp}")
check_sink("${sup_h}" "${sup_cpp}" "${m}" r)
expect_caught("the stall sink moved out of PlannerShadow.cpp" "${r}")
check_sink("${sup_h}" "${sup_cpp}\nai::replan::Verdict Supervisor::OnRelocate(ai::replan::RelocationEvent const&) { return {}; }\n" "${shadow_cpp}" r)
expect_caught("a sink body in Supervisor.cpp" "${r}")

check_latch("${shadow_h}\nenum class RelocSource : std::uint8_t { Unknown = 0 };\n" "${ai_h}" "${ai_cpp}" r)
expect_caught("the placeholder enum back" "${r}")
string(REPLACE "void PlayerbotAI::SetLastRelocSource(ai::replan::RelocSource source)\n{\n    if (!ReplanWired())\n        return;\n" "void PlayerbotAI::SetLastRelocSource(ai::replan::RelocSource source)\n{\n" m "${ai_cpp}")
check_latch("${shadow_h}" "${ai_h}" "${m}" r)
expect_caught("an ungated setter (D3)" "${r}")
check_latch("${shadow_h}" "${ai_h}" "${ai_cpp}\nvoid PlayerbotAI::ExtraRelocWriter()\n{\n    MergeRelocLatch(lastRelocSource_, [](std::uint64_t l) { return l; });\n}\n" r)
expect_caught("a 4th latch writer without its gate (OB-10 R-7)" "${r}")
string(REPLACE "    if (ai::replan_wire::DropAtTickEnd(cur, nowMs))" "    if (ai::replan_wire::Stale(cur, nowMs))" m "${ai_cpp}")
check_latch("${shadow_h}" "${ai_h}" "${m}" r)
expect_caught("an acked hop mark kept past its tick (OB-10 R-3)" "${r}")
string(REPLACE "HandleMoveWorldportAckOpcode();\n        NoteTeleportAck();" "HandleMoveWorldportAckOpcode();" m "${ai_cpp}")
check_latch("${shadow_h}" "${ai_h}" "${m}" r)
expect_caught("the far ack not noted" "${r}")

string(REPLACE "        if (rescued)\n            ai->SetLastRelocSource" "        ai->SetLastRelocSource" m "${rndmgr}")
check_sites("${ai_cpp}" "${useitem}" "${movement}" "${travelmgr}" "${m}" r)
expect_caught("the rescue label without a started teleport (D12)" "${r}")
string(REPLACE "MoveResult::Moved && bot->IsBeingTeleported())" "MoveResult::Moved)" m "${travelmgr}")
check_sites("${ai_cpp}" "${useitem}" "${movement}" "${m}" "${rndmgr}" r)
expect_caught("the pos stuck label without a started teleport (D12)" "${r}")
check_sites("${ai_cpp}" "${useitem}" "${movement}" "${travelmgr}\nai->SetLastRelocSource(ai::replan::RelocSource::Death);\n" "${rndmgr}" r)
expect_caught("a death label from a site (D5)" "${r}")
string(REPLACE "        if (hopped)\n            ai->MarkRelocHop();   // R9-2 (twow-repo#575, D6): the fly-direct hop" "        if (hopped)   // R9-2 (twow-repo#575, D6): the fly-direct hop" m "${movement}")
check_sites("${ai_cpp}" "${useitem}" "${m}" "${travelmgr}" "${rndmgr}" r)
expect_caught("a hop without its mark (D6)" "${r}")

check_wire("${wire}\n#include \"playerbot/PlayerbotAIConfig.h\"\n" "${tests}" r)
expect_caught("a game include in the pure rules" "${r}")
string(REPLACE "    return r.forced;\n}" "    return r.forced && r.episode == 0;\n}" m "${wire}")
check_wire("${m}" "${tests}" r)
expect_caught("only some forced records outside the budget (D11)" "${r}")
string(REPLACE "kForcedLinesPerMinute = 600;" "kForcedLinesPerMinute = 6000;" m "${wire}")
check_wire("${m}" "${tests}" r)
expect_caught("the forced-line ceiling changed (R9-a)" "${r}")
string(REPLACE "add_test(NAME replan_wire_policy\n" "add_test(NAME replan_wire_policy_off\n" m "${tests}")
check_wire("${wire}" "${m}" r)
expect_caught("suite not registered" "${r}")

message(STATUS "${TAG}: all checks passed, ${probes} negative probes caught")
