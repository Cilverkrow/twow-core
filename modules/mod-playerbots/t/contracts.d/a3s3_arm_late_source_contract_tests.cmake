cmake_policy(VERSION 3.16)
# twow-repo#726 (OB-60 deep search 7): the late arm, AiPlayerbot.Supervisor.ArmLate (bug fix; default 0 until acceptance + OB-10).
# The arming edge (Supervisor::ArmEnforce) checks a bot once, in the tick enforcement turns on; a bot that is parked, in a
# battleground or out of the quest book scope in that tick stayed Passive while enforcement stayed on (no other path leaves
# Passive). With the key at 1, Supervisor::ArmLateTick (SupervisorEnforce.cpp) retries the same arm rule while enforcing and
# Passive, at most every kArmLateRetryMs (SupervisorPolicy.h ArmLatePoll / ArmEligible). Pinned here:
#   (a) the tick: the enforcement edges byte-identical, then the one ArmLateTick call, then the probe; ArmLateTick has one caller;
#   (b) the edge's ArmEnforce byte-identical (cause arbiter_enforce, the same three reads);
#   (c) ArmLateTick: the key poll is its first statement; then the same three reads through ArmEligible; Decide Passive -> Quest
#       by External; arm_late counted only on an allowed arm; the budgeted mode line with cause arbiter_arm_late (no source
#       string starts with "arbiter_enforce but the edge's, so a substring match on cause=arbiter_enforce never counts it);
#   (d) the pure rules: ArmLatePoll off for every key but 1, outside enforcement and outside Passive (deadline dropped);
#       5 s interval; ArmEligible = the edge's rule;
#   (e) the key: uint32 member default 0, read with default 0, above 1 = 0 + error line, read only in ArmLateTick, echoed
#       right after Supervisor.ArbiterProbe (sorted), documented once as 0 in the conf.dist;
#   (f) the count: EnforceCount::ArmLate before kGrpFirst, counted by CountEnforce; [ArbiterMin] ends with arm_late= only while
#       the key is not 0 (OB-10 rule, as P-3a's plan=): the base format text unchanged, P-3a's plan= tail, then a tail behind armLateField, which
#       RollLocked takes from g_armLateField, set only by the config load (SetArbiterArmLateField(supervisorArmLate != 0));
#   (g) the unit test TestArmLate exists and runs.
# Every check_* returns "" when it holds, else the reason; the negative probes at the end must each be caught for their reason.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s3 arm late: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s3 arm late: PB_MODULE_DIR is required")
endif()

set(TAG "a3s3 arm late")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("Supervisor.cpp" sup_cpp)
read_source("Supervisor.h" sup_h)
read_source("SupervisorEnforce.cpp" enforce_cpp)
read_source("SupervisorPolicy.h" policy)
read_source("SupervisorShadow.h" shadow_h)
read_source("SupervisorShadow.cpp" shadow_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
file(READ "${PB_MODULE_DIR}/t/supervisor_policy_tests.cpp" unit_test)
string(REPLACE "\r" "" unit_test "${unit_test}")

file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(SORT all_files)
set(all_sources "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(REPLACE "\r" "" t "${t}")
  string(APPEND all_sources "${t}\n")
endforeach()

# Plain-text count (a ";" inside a MATCHALL hit would split the list and double count).
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

# need: the first missing needle sets ERR in the calling function (ERR is read from and written to the caller's scope).
function(need text needle reason)
  if(NOT "${ERR}" STREQUAL "")
    return()
  endif()
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(ERR "${reason}" PARENT_SCOPE)
  endif()
endfunction()

set(EDGES "    if (enforce != enforce_)\n    {\n        enforce_ = enforce;\n        if (enforce)\n            ArmEnforce();\n        else\n            DisarmEnforce(arbiter);\n    }\n    ArmLateTick(nowMs);\n    if (arbiter >= 1)\n        ProbeTick(nowMs, arbiter);\n")
set(ARM_ENFORCE "void Supervisor::ArmEnforce()\n{\n    Player* const bot = ai_ ? ai_->GetBot() : nullptr;\n    if (!bot || mode_ != Mode::Passive)\n        return;\n    if (!ai_->InQuestBookScope() || bot->InBattleGround() || ai_->IsParked())\n        return;\n    TransitionResult const result = Decide(Mode::Passive, Mode::Quest, Requester::External, phase_, 0, Cadence{});\n    if (result.allowed)\n        mode_ = result.to;\n    WriteModeLine(Mode::Passive, Mode::Quest, result, \"arbiter_enforce\", 2);\n}\n")
set(ARM_LATE "void Supervisor::ArmLateTick(std::uint32_t nowMs)\n{\n    if (!ArmLatePoll(sPlayerbotAIConfig.supervisorArmLate, enforce_, mode_, nowMs, armLateNextMs_))\n        return;\n    Player* const bot = ai_ ? ai_->GetBot() : nullptr;\n    if (!bot || !ArmEligible(ai_->InQuestBookScope(), bot->InBattleGround(), ai_->IsParked()))\n        return;\n    TransitionResult const result = Decide(Mode::Passive, Mode::Quest, Requester::External, phase_, 0, Cadence{});\n    if (result.allowed)\n    {\n        mode_ = result.to;\n        CountEnforce(bot->GetGUIDLow(), EnforceCount::ArmLate);\n    }\n    WriteModeLine(Mode::Passive, Mode::Quest, result, \"arbiter_arm_late\", 2);\n}\n")
set(POLL "inline bool ArmLatePoll(std::uint32_t armLate, bool enforce, Mode mode, std::uint32_t nowMs, std::uint32_t& nextMs)\n{\n    if (armLate != 1 || !enforce || mode != Mode::Passive)\n    {\n        nextMs = 0;\n        return false;\n    }\n    std::int32_t const wait = std::int32_t(nextMs - nowMs);\n    if (nextMs != 0 && wait > 0 && wait <= std::int32_t(kArmLateRetryMs))\n        return false;\n    bool const due = nextMs != 0 && wait <= 0;\n    nextMs = nowMs + kArmLateRetryMs;\n    if (nextMs == 0)\n        nextMs = 1;\n    return due;\n}\n")
set(ELIGIBLE "inline bool ArmEligible(bool inQuestBookScope, bool inBattleGround, bool parked)\n{\n    return inQuestBookScope && !inBattleGround && !parked;\n}\n")
set(READ_KEY "    supervisorArmLate = uint32(config.GetIntDefault(\"AiPlayerbot.Supervisor.ArmLate\", 0));\n    if (supervisorArmLate > 1)\n    {\n        sLog.outError(\"[Supervisor] config key=Supervisor.ArmLate value=%u result=off (0 off, 1 late arm)\", supervisorArmLate);\n        supervisorArmLate = 0;\n    }\n    ai::supervisor::SetArbiterArmLateField(supervisorArmLate != 0);\n")
set(BASE_FMT "        \" gate_off_world=%u gate_off_none=%u gate_off_bot=%u\",\n        cohort, m.bundles,")
set(TAIL "        m.depthOverflow, m.gateOffWorld, m.gateOffNone, m.gateOffBot);\n    std::size_t len = Sanitize(buf, w, n);\n    if (m.planField && len + 1 < n)\n    {\n        int const p = std::snprintf(buf + len, n - len, \" plan=%u\", f(WriterId::Planner));\n        len += Sanitize(buf + len, p, n - len);\n    }\n    // twow-repo#726: arm_late only while AiPlayerbot.Supervisor.ArmLate != 0 (armLateField), after plan=; key 0 = byte-identical.\n    if (!m.armLateField || len + 1 >= n)\n        return len;\n    int const a = std::snprintf(buf + len, n - len, \" arm_late=%u\", m.armLate);\n    return len + Sanitize(buf + len, a, n - len);\n}\n")
set(ROLL "    bool const armLateField = g_armLateField.load(std::memory_order_relaxed);   // twow-repo#726: arm_late= at ArmLate != 0\n    for (std::size_t i = 0; i < 2; ++i)\n    {\n        out.active[i] = g_active[i];\n        out.sums[i] = g_min[i];\n        out.sums[i].armLateField = armLateField;\n")
set(ECHO "            {\"Supervisor.ArbiterProbe\", Value(supervisorArbiterProbe)},\n            {\"Supervisor.ArmLate\", Value(supervisorArmLate)},\n            {\"ThreadSafeTeleportAck\", Value(threadSafeTeleportAck)},\n")

# ---- (a) the tick, (b) the edge -------------------------------------------------------------------------------------------------
function(check_tick sup_cpp sup_h all_sources out)
  set(ERR "")
  region("${sup_cpp}" "void Supervisor::Tick(std::uint32_t nowMs, std::uint32_t arbiter)\n{\n" "\n}\n" tick)
  need("${tick}" "${EDGES}" "Supervisor.cpp: the tick is not the unchanged enforcement edges, then the one ArmLateTick call, then the probe")
  need("${sup_cpp}" "${ARM_ENFORCE}" "Supervisor.cpp: ArmEnforce (the arming edge, cause arbiter_enforce) is not byte-identical to the base")
  need("${sup_h}" "    void ArmLateTick(std::uint32_t nowMs);\n" "Supervisor.h: ArmLateTick is not declared")
  need("${sup_h}" "    std::uint32_t armLateNextMs_ = 0;" "Supervisor.h: the late arm deadline is not a member starting at 0")
  if(NOT ERR STREQUAL "")
    set(${out} "${ERR}" PARENT_SCOPE)
    return()
  endif()
  count_text("${all_sources}" "ArmLateTick(" n)
  if(NOT n EQUAL 3)
    set(${out} "ArmLateTick( appears ${n} times in src/ (3 expected: declaration, definition, the one call in the tick)" PARENT_SCOPE)
    return()
  endif()
  count_text("${all_sources}" "ArmEnforce(" n)
  if(NOT n EQUAL 3)
    set(${out} "ArmEnforce( appears ${n} times in src/ (3 expected: declaration, definition, the arming edge)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (c) the late arm, (d) the pure rules ----------------------------------------------------------------------------------------
function(check_late enforce_cpp policy all_sources out)
  set(ERR "")
  need("${enforce_cpp}" "${ARM_LATE}" "SupervisorEnforce.cpp: ArmLateTick is not the key poll first, the edge's three reads through ArmEligible, Decide Passive -> Quest by External, arm_late on an allowed arm and the budgeted line with cause arbiter_arm_late")
  need("${policy}" "constexpr std::uint32_t kArmLateRetryMs = 5000;" "SupervisorPolicy.h: the late arm interval is not 5000 ms")
  need("${policy}" "${POLL}" "SupervisorPolicy.h: ArmLatePoll is not off for every key but 1, outside enforcement and outside Passive (deadline dropped), and rate-limited")
  need("${policy}" "${ELIGIBLE}" "SupervisorPolicy.h: ArmEligible is not the edge's arm rule (scope, no battleground, not parked)")
  if(NOT ERR STREQUAL "")
    set(${out} "${ERR}" PARENT_SCOPE)
    return()
  endif()
  count_text("${all_sources}" "\"arbiter_arm_late\"" n_late)
  count_text("${all_sources}" "\"arbiter_enforce\"" n_edge)
  count_text("${all_sources}" "\"arbiter_enforce" n_prefix)
  if(NOT n_late EQUAL 1 OR NOT n_edge EQUAL 1 OR NOT n_prefix EQUAL 1)
    set(${out} "cause strings: \"arbiter_arm_late\" ${n_late}, \"arbiter_enforce\" ${n_edge}, strings starting \"arbiter_enforce ${n_prefix} (1 each: the late arm, the edge; no other cause matches cause=arbiter_enforce)" PARENT_SCOPE)
    return()
  endif()
  count_text("${all_sources}" "EnforceCount::ArmLate" n)
  if(NOT n EQUAL 2)
    set(${out} "EnforceCount::ArmLate appears ${n} times in src/ (2 expected: the late arm's count, the CountEnforce case)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (e) the key -----------------------------------------------------------------------------------------------------------------
function(check_key config_h config_cpp conf_dist all_sources out)
  set(ERR "")
  need("${config_h}" "    uint32 supervisorArmLate = 0;\n" "PlayerbotAIConfig.h: supervisorArmLate is not a uint32 member with default 0")
  need("${config_cpp}" "${READ_KEY}" "PlayerbotAIConfig.cpp: the ArmLate read is not default 0 with above 1 = 0 and an error line, then the arm_late field switch from the clamped key")
  need("${config_cpp}" "${ECHO}" "PlayerbotAIConfig.cpp: Supervisor.ArmLate is not echoed in its sorted place after Supervisor.ArbiterProbe")
  if(NOT ERR STREQUAL "")
    set(${out} "${ERR}" PARENT_SCOPE)
    return()
  endif()
  count_text("${all_sources}" "sPlayerbotAIConfig.supervisorArmLate" n)
  if(NOT n EQUAL 1)
    set(${out} "sPlayerbotAIConfig.supervisorArmLate is read ${n} times (1 expected: the first statement of ArmLateTick)" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "\nAiPlayerbot\\.Supervisor\\.ArmLate = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.Supervisor.ArmLate = 0")
    set(${out} "conf.dist: AiPlayerbot.Supervisor.ArmLate not documented once as 0" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (f) the count, (g) the unit test -------------------------------------------------------------------------------------------
function(check_count policy shadow_h shadow_cpp all_sources unit_test out)
  set(ERR "")
  need("${policy}" "${BASE_FMT}" "SupervisorPolicy.h: the [ArbiterMin] format text does not end with gate_off_bot as at the base")
  need("${policy}" "${TAIL}" "SupervisorPolicy.h: arm_late is not a tail after the base line, only behind armLateField (key 0 = byte-identical)")
  need("${policy}" "    std::uint32_t armLate = 0;\n" "SupervisorPolicy.h: ArbiterMinute has no armLate counter starting at 0")
  need("${policy}" "    bool armLateField = false;\n" "SupervisorPolicy.h: ArbiterMinute has no armLateField starting false")
  need("${shadow_cpp}" "std::atomic<bool> g_armLateField{false};\n" "SupervisorShadow.cpp: the arm_late field switch is not an atomic starting false")
  need("${shadow_cpp}" "${ROLL}" "SupervisorShadow.cpp: RollLocked does not take the arm_late field switch into the minute sums")
  need("${shadow_cpp}" "void SetArbiterArmLateField(bool on)\n{\n    g_armLateField.store(on, std::memory_order_relaxed);\n}\n" "SupervisorShadow.cpp: SetArbiterArmLateField does not only store the switch")
  need("${shadow_h}" "void SetArbiterArmLateField(bool on);\n" "SupervisorShadow.h: SetArbiterArmLateField is not declared")
  need("${shadow_h}" "    GateOffBot,\n    // twow-repo#726 (AiPlayerbot.Supervisor.ArmLate, default 0): [ArbiterMin] arm_late (before kGrpFirst: not a group field).\n    ArmLate,\n    // T02-GROUP [ArbiterGrp]" "SupervisorShadow.h: EnforceCount::ArmLate is not right after GateOffBot, before the group block")
  need("${shadow_h}" "constexpr EnforceCount kGrpFirst = EnforceCount::DirEnter;" "SupervisorShadow.h: kGrpFirst is no longer DirEnter")
  need("${shadow_cpp}" "        case EnforceCount::ArmLate: ++m.armLate; break;\n" "SupervisorShadow.cpp: CountEnforce does not count arm_late")
  need("${unit_test}" "void TestArmLate()" "t/supervisor_policy_tests.cpp: TestArmLate is missing")
  need("${unit_test}" "    TestArmLate();\n" "t/supervisor_policy_tests.cpp: TestArmLate is not run")
  need("${unit_test}" "Require(std::string(buf) == baseLine, \"key 0: the minute line is byte-identical (no arm_late)\");" "t/supervisor_policy_tests.cpp: no key-0 byte golden of the minute line")
  if(NOT ERR STREQUAL "")
    set(${out} "${ERR}" PARENT_SCOPE)
    return()
  endif()
  # One writer of the switch (the config load), one reader (RollLocked).
  count_text("${all_sources}" "SetArbiterArmLateField(" n_set)
  count_text("${all_sources}" "g_armLateField" n_g)
  if(NOT n_set EQUAL 3 OR NOT n_g EQUAL 3)
    set(${out} "SetArbiterArmLateField( ${n_set} (3: decl, def, config load), g_armLateField ${n_g} (3: atomic, store, the RollLocked load)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

check_tick("${sup_cpp}" "${sup_h}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_late("${enforce_cpp}" "${policy}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_key("${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_count("${policy}" "${shadow_h}" "${shadow_cpp}" "${all_sources}" "${unit_test}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()

# ---- negative probes: each mutation must be caught for its own reason --------------------------------------------------------
set(probes 0)
function(expect_reason result want description)
  string(FIND "${result}" "${want}" at)
  if(result STREQUAL "" OR at EQUAL -1)
    message(FATAL_ERROR "${TAG}: negative probe not caught for its reason (${want}): ${description}; got: ${result}")
  endif()
endfunction()

# 1 the retry is not called
string(REPLACE "    ArmLateTick(nowMs);\n    if (arbiter >= 1)" "    if (arbiter >= 1)" m "${sup_cpp}")
check_tick("${m}" "${sup_h}" "${all_sources}" r)
expect_reason("${r}" "the one ArmLateTick call" "the tick does not call the late arm")
math(EXPR probes "${probes} + 1")

# 2 the retry moved into the edge (it would run only on the edge)
string(REPLACE "            ArmEnforce();\n        else\n            DisarmEnforce(arbiter);\n    }\n    ArmLateTick(nowMs);\n"
  "            ArmEnforce();\n        else\n            DisarmEnforce(arbiter);\n        ArmLateTick(nowMs);\n    }\n" m "${sup_cpp}")
check_tick("${m}" "${sup_h}" "${all_sources}" r)
expect_reason("${r}" "unchanged enforcement edges" "the late arm inside the edge block")
math(EXPR probes "${probes} + 1")

# 3 the edge changed (one read dropped)
string(REPLACE "    if (!ai_->InQuestBookScope() || bot->InBattleGround() || ai_->IsParked())\n        return;\n    TransitionResult"
  "    if (!ai_->InQuestBookScope() || bot->InBattleGround())\n        return;\n    TransitionResult" m "${sup_cpp}")
check_tick("${m}" "${sup_h}" "${all_sources}" r)
expect_reason("${r}" "ArmEnforce (the arming edge, cause arbiter_enforce) is not byte-identical" "the edge's parked check dropped")
math(EXPR probes "${probes} + 1")

# 4 a second caller of the late arm
check_tick("${sup_cpp}" "${sup_h}" "${all_sources}\n    ArmLateTick(1);" r)
expect_reason("${r}" "ArmLateTick( appears 4 times" "a second ArmLateTick call")
math(EXPR probes "${probes} + 1")

# 5 the retry not behind the key (the poll gets a constant 1)
string(REPLACE "ArmLatePoll(sPlayerbotAIConfig.supervisorArmLate, enforce_" "ArmLatePoll(1, enforce_" m "${enforce_cpp}")
check_late("${m}" "${policy}" "${all_sources}" r)
expect_reason("${r}" "ArmLateTick is not the key poll first" "the late arm ignores the key")
math(EXPR probes "${probes} + 1")

# 6 the late arm skips the parked check
string(REPLACE "ArmEligible(ai_->InQuestBookScope(), bot->InBattleGround(), ai_->IsParked())" "ArmEligible(ai_->InQuestBookScope(), bot->InBattleGround(), false)" m "${enforce_cpp}")
check_late("${m}" "${policy}" "${all_sources}" r)
expect_reason("${r}" "the edge's three reads" "the late arm arms a parked bot")
math(EXPR probes "${probes} + 1")

# 7 the late cause folded into the edge cause
string(REPLACE "\"arbiter_arm_late\"" "\"arbiter_enforce\"" m "${all_sources}")
check_late("${enforce_cpp}" "${policy}" "${m}" r)
expect_reason("${r}" "cause strings" "the late arm writes cause arbiter_enforce")
math(EXPR probes "${probes} + 1")

# 8 counted on every retry (also an ineligible one: not silent)
string(REPLACE "    Player* const bot = ai_ ? ai_->GetBot() : nullptr;\n    if (!bot || !ArmEligible("
  "    Player* const bot = ai_ ? ai_->GetBot() : nullptr;\n    CountEnforce(bot->GetGUIDLow(), EnforceCount::ArmLate);\n    if (!bot || !ArmEligible(" m "${enforce_cpp}")
check_late("${m}" "${policy}" "${all_sources}" r)
expect_reason("${r}" "arm_late on an allowed arm" "an ineligible retry counts")
math(EXPR probes "${probes} + 1")

# 9 the poll accepts every non-zero key
string(REPLACE "    if (armLate != 1 || !enforce || mode != Mode::Passive)" "    if (armLate == 0 || !enforce || mode != Mode::Passive)" m "${policy}")
check_late("${enforce_cpp}" "${m}" "${all_sources}" r)
expect_reason("${r}" "off for every key but 1" "key 2 acts like 1")
math(EXPR probes "${probes} + 1")

# 10 no rate limit (due on every tick)
string(REPLACE "    if (nextMs != 0 && wait > 0 && wait <= std::int32_t(kArmLateRetryMs))\n        return false;\n" "" m "${policy}")
check_late("${enforce_cpp}" "${m}" "${all_sources}" r)
expect_reason("${r}" "rate-limited" "the poll without the interval")
math(EXPR probes "${probes} + 1")

# 11 the late arm after a disarm (enforce not checked)
string(REPLACE "    if (armLate != 1 || !enforce || mode != Mode::Passive)" "    if (armLate != 1 || mode != Mode::Passive)" m "${policy}")
check_late("${enforce_cpp}" "${m}" "${all_sources}" r)
expect_reason("${r}" "outside enforcement" "the late arm undoes the disarming edge")
math(EXPR probes "${probes} + 1")

# 12 code default 1
string(REPLACE "    uint32 supervisorArmLate = 0;" "    uint32 supervisorArmLate = 1;" m "${config_h}")
check_key("${m}" "${config_cpp}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "member with default 0" "member default 1")
math(EXPR probes "${probes} + 1")

# 13 read default 1
string(REPLACE "\"AiPlayerbot.Supervisor.ArmLate\", 0)" "\"AiPlayerbot.Supervisor.ArmLate\", 1)" m "${config_cpp}")
check_key("${config_h}" "${m}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "read is not default 0" "read default 1")
math(EXPR probes "${probes} + 1")

# 14 no clamp
string(REPLACE "    if (supervisorArmLate > 1)\n" "    if (supervisorArmLate > 2)\n" m "${config_cpp}")
check_key("${config_h}" "${m}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "above 1 = 0" "value 2 passes the clamp")
math(EXPR probes "${probes} + 1")

# 15 not echoed
string(REPLACE "            {\"Supervisor.ArmLate\", Value(supervisorArmLate)},\n" "" m "${config_cpp}")
check_key("${config_h}" "${m}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "not echoed" "the echo entry dropped")
math(EXPR probes "${probes} + 1")

# 16 conf.dist default 1
string(REPLACE "AiPlayerbot.Supervisor.ArmLate = 0" "AiPlayerbot.Supervisor.ArmLate = 1" m "${conf_dist}")
check_key("${config_h}" "${config_cpp}" "${m}" "${all_sources}" r)
expect_reason("${r}" "documented once as 0" "conf.dist default 1")
math(EXPR probes "${probes} + 1")

# 17 a second read of the key
check_key("${config_h}" "${config_cpp}" "${conf_dist}" "${all_sources}\n    if (sPlayerbotAIConfig.supervisorArmLate)" r)
expect_reason("${r}" "is read 2 times" "the key read outside ArmLateTick")
math(EXPR probes "${probes} + 1")

# 18 arm_late in the base format text (printed at key 0 too: not byte-identical)
string(REPLACE "        \" gate_off_world=%u gate_off_none=%u gate_off_bot=%u\",\n" "        \" gate_off_world=%u gate_off_none=%u gate_off_bot=%u arm_late=%u\",\n" m "${policy}")
check_count("${m}" "${shadow_h}" "${shadow_cpp}" "${all_sources}" "${unit_test}" r)
expect_reason("${r}" "does not end with gate_off_bot as at the base" "arm_late always printed")
math(EXPR probes "${probes} + 1")

# 19 the tail without the key switch
string(REPLACE "    if (!m.armLateField || len + 1 >= n)\n" "    if (len + 1 >= n)\n" m "${policy}")
check_count("${m}" "${shadow_h}" "${shadow_cpp}" "${all_sources}" "${unit_test}" r)
expect_reason("${r}" "only behind armLateField" "the arm_late tail at key 0")
math(EXPR probes "${probes} + 1")

# 20 not counted
string(REPLACE "        case EnforceCount::ArmLate: ++m.armLate; break;\n" "" m "${shadow_cpp}")
check_count("${policy}" "${shadow_h}" "${m}" "${all_sources}" "${unit_test}" r)
expect_reason("${r}" "does not count arm_late" "the CountEnforce case dropped")
math(EXPR probes "${probes} + 1")

# 21 the unit test not run
string(REPLACE "    TestArmLate();\n" "" m "${unit_test}")
check_count("${policy}" "${shadow_h}" "${shadow_cpp}" "${all_sources}" "${m}" r)
expect_reason("${r}" "TestArmLate is not run" "the unit test not run")
math(EXPR probes "${probes} + 1")

# 22 the minute sums never get the switch (arm_late never printed at key 1)
string(REPLACE "        out.sums[i].armLateField = armLateField;\n" "" m "${shadow_cpp}")
check_count("${policy}" "${shadow_h}" "${m}" "${all_sources}" "${unit_test}" r)
expect_reason("${r}" "RollLocked does not take the arm_late field switch" "RollLocked drops the switch")
math(EXPR probes "${probes} + 1")

# 23 the config load does not set the switch
string(REPLACE "    ai::supervisor::SetArbiterArmLateField(supervisorArmLate != 0);\n" "" m "${config_cpp}")
check_key("${config_h}" "${m}" "${conf_dist}" "${all_sources}" r)
expect_reason("${r}" "then the arm_late field switch from the clamped key" "the switch not set by the config load")
math(EXPR probes "${probes} + 1")

# 24 a second writer of the switch
check_count("${policy}" "${shadow_h}" "${shadow_cpp}" "${all_sources}\n    ai::supervisor::SetArbiterArmLateField(true);" "${unit_test}" r)
expect_reason("${r}" "SetArbiterArmLateField( 4" "a second writer of the switch")
math(EXPR probes "${probes} + 1")

# 25 the key-0 golden dropped from the unit test
string(REPLACE "Require(std::string(buf) == baseLine, \"key 0: the minute line is byte-identical (no arm_late)\");" "" m "${unit_test}")
check_count("${policy}" "${shadow_h}" "${shadow_cpp}" "${all_sources}" "${m}" r)
expect_reason("${r}" "no key-0 byte golden" "the key-0 golden dropped")
math(EXPR probes "${probes} + 1")

# 26 a late cause that starts with arbiter_enforce (a substring match on cause=arbiter_enforce would count it)
string(REPLACE "\"arbiter_arm_late\"" "\"arbiter_enforce_late\"" m "${all_sources}")
check_late("${enforce_cpp}" "${policy}" "${m}" r)
expect_reason("${r}" "no other cause matches cause=arbiter_enforce" "the late cause named arbiter_enforce_late")
math(EXPR probes "${probes} + 1")

message(STATUS "${TAG}: OK (${probes} negative probes)")
