cmake_policy(VERSION 3.16)
# A3 step 3 T02-GROUP follow-up gather_kind3 (AiPlayerbot.Supervisor.Arbiter = 2 and AiPlayerbot.BotGroups.Manager.Types > 0,
# both default 0; T33 design v1.3 Nachtrag 2, CLI-GRP 11.11.2026): gather kind 3 is not defined and the manager never packs it.
# A taken Enter or Change of kind 3 moves as kind a, gets no ack 3 (that would end the group by ack_refused) and counts the
# [ArbiterGrp] field gather_kind3, so a packing error shows. This contract pins, each with a negative probe at the end:
#   (1) GatherKindUndefined defined once in SupervisorGroupPolicy.h: Enter or Change, and the kind equal to kGatherUndefined (3);
#   (2) the one count CountGrp(guidLow, GrpField::GatherKind3) sits in HandleMail behind GatherKindUndefined, and nowhere else;
#   (3) kGatherUndefined is used only by that helper (no ack, move or check branches on kind 3; ChooseMove has no kind 3 case),
#       and no code compares a gather kind with the literal 3;
#   (4) gather_kind3 is the last GrpField (51 fields, the last name), mirrored by EnforceCount (A3_GRP_SAME, the span assert);
#   (5) the unit test calls TestGatherKind3.
# Every check_* returns "" when it holds, else the reason; the negative probes reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s3 gather_kind3: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s3 gather_kind3: PB_MODULE_DIR is required")
endif()

set(TAG "a3s3 gather_kind3")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(fail_if reason)
  if(NOT "${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${reason}")
  endif()
endfunction()

# A probe must be caught by the check it aims at, not by a needle it broke on the way.
function(expect_reason what reason pattern)
  if("${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
  if(NOT "${reason}" MATCHES "${pattern}")
    message(FATAL_ERROR "${TAG}: negative probe ${what} caught by another check: ${reason}")
  endif()
endfunction()

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

# From the first start to the first stop after it, both included; "" when either is missing.
function(region text start stop out)
  set(${out} "" PARENT_SCOPE)
  string(FIND "${text}" "${start}" at)
  if(at EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "${stop}" end)
  if(end EQUAL -1)
    return()
  endif()
  string(LENGTH "${stop}" sl)
  math(EXPR end "${end} + ${sl}")
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# Code without // comments.
function(strip_comments text out)
  string(REGEX REPLACE "//[^\n]*" "" code "${text}")
  set(${out} "${code}" PARENT_SCOPE)
endfunction()

read_source("SupervisorGroup.cpp" group_cpp)
read_source("SupervisorGroupPolicy.h" gpolicy)
read_source("SupervisorPolicy.h" policy)
read_source("SupervisorShadow.h" shadow_h)
read_source("SupervisorShadow.cpp" shadow_cpp)
file(READ "${PB_MODULE_DIR}/t/supervisor_group_policy_tests.cpp" unit_cpp)
string(REPLACE "\r" "" unit_cpp "${unit_cpp}")

file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(SORT all_files)
set(all_sources "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(REPLACE "\r" "" t "${t}")
  string(APPEND all_sources "${t}\n")
endforeach()

# ---- (1) the helper ----------------------------------------------------------------------------------------------------------
set(DEF_UNDEF "inline bool GatherKindUndefined(VerbClass v, std::uint8_t gatherKind)\n{")
set(UNDEF_BODY "    return (v == VerbClass::Enter || v == VerbClass::Change) && gatherKind == kGatherUndefined;\n}")
function(check_helper gpolicy all_sources out)
  count_text("${all_sources}" "bool GatherKindUndefined(" n)
  count_text("${gpolicy}" "constexpr std::uint8_t kGatherUndefined = 3;" nk)
  region("${gpolicy}" "${DEF_UNDEF}" "\n}\n" body)
  string(FIND "${body}" "${UNDEF_BODY}" at)
  if(NOT n EQUAL 1 OR NOT nk EQUAL 1 OR at EQUAL -1)
    set(${out} "helper: GatherKindUndefined defined ${n} times, kGatherUndefined = 3 ${nk} times (1 and 1 expected), body pinned: ${at}" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (2) the one count -------------------------------------------------------------------------------------------------------
set(DEF_HANDLE "void Supervisor::HandleMail(Event const* mail, std::size_t n)\n{")
set(COUNT_K3 "            if (GatherKindUndefined(v, d.gatherKind))\n                CountGrp(guidLow, GrpField::GatherKind3);\n")
function(check_count group_cpp all_sources out)
  count_text("${all_sources}" "GrpField::GatherKind3" n)
  count_text("${all_sources}" "GatherKindUndefined(v, d.gatherKind)" nc)
  region("${group_cpp}" "${DEF_HANDLE}" "\n}\n" body)
  string(FIND "${body}" "${COUNT_K3}" at)
  if(NOT n EQUAL 1 OR NOT nc EQUAL 1 OR at EQUAL -1)
    set(${out} "count: GrpField::GatherKind3 appears ${n} times and the guard ${nc} times under PB/ (1 and 1 expected), in HandleMail: ${at}" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (3) kind 3 drives nothing -----------------------------------------------------------------------------------------------
set(DEF_CHOOSE "inline MoveKind ChooseMove(bool gatherFlag, std::uint8_t gatherKind, bool isLeader, bool atPoint = false)\n{")
function(check_inert all_sources gpolicy out)
  strip_comments("${all_sources}" code)
  count_text("${code}" "kGatherUndefined" n)
  region("${gpolicy}" "${DEF_CHOOSE}" "\n}\n" choose)
  string(REGEX MATCH "[Kk]ind_? *[!=]= *3[^0-9]" lit "${code}")
  string(FIND "${choose}" "kGatherUndefined" ac)
  if(choose STREQUAL "")
    set(${out} "inert: ChooseMove not found" PARENT_SCOPE)
    return()
  endif()
  if(NOT n EQUAL 2 OR NOT ac EQUAL -1 OR NOT lit STREQUAL "")
    set(${out} "inert: kGatherUndefined appears ${n} times in code (2 expected: the constant, the helper), kind 3 case in ChooseMove: ${ac}, a literal kind 3 compare: '${lit}'" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (4) the field, its name, the shadow mirror ------------------------------------------------------------------------------
function(check_field policy shadow_h shadow_cpp out)
  set(r "")
  foreach(needle "    HandlerOff,\n    GatherKind3,\n    Count\n};\nconstexpr std::size_t kGrpFields = 51;" "\"follow_self\", \"handler_off\", \"gather_kind3\"};")
    string(FIND "${policy}" "${needle}" at)
    if(at EQUAL -1)
      set(r "field: SupervisorPolicy.h lacks the last field / name gather_kind3")
    endif()
  endforeach()
  count_text("${shadow_cpp}" "A3_GRP_SAME(GatherKind3);" ns)
  string(FIND "${shadow_h}" "    HandlerOff,\n    GatherKind3,\n};" ae)
  string(FIND "${shadow_h}" "static_assert(std::size_t(EnforceCount::GatherKind3) - std::size_t(kGrpFirst) + 1 == kGrpFields," aa)
  if(r STREQUAL "" AND (NOT ns EQUAL 1 OR ae EQUAL -1 OR aa EQUAL -1))
    set(r "field: the shadow mirror of GatherKind3 is broken (A3_GRP_SAME ${ns} times, last EnforceCount ${ae}, span assert ${aa})")
  endif()
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- (5) the unit test -------------------------------------------------------------------------------------------------------
function(check_unit unit_cpp out)
  count_text("${unit_cpp}" "void TestGatherKind3()\n{" nd)
  count_text("${unit_cpp}" "    TestGatherKind3();\n" nc)
  if(NOT nd EQUAL 1 OR NOT nc EQUAL 1)
    set(${out} "unit: TestGatherKind3 defined ${nd} times and called ${nc} times (1 and 1 expected)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the checks ----------------------------------------------------------------------------------------------------------------
check_helper("${gpolicy}" "${all_sources}" r)
fail_if("${r}")
check_count("${group_cpp}" "${all_sources}" r)
fail_if("${r}")
check_inert("${all_sources}" "${gpolicy}" r)
fail_if("${r}")
check_field("${policy}" "${shadow_h}" "${shadow_cpp}" r)
fail_if("${r}")
check_unit("${unit_cpp}" r)
fail_if("${r}")

# ---- negative probes -------------------------------------------------------------------------------------------------------------
# (1) Exit counts too, the kind constant changed, a second helper.
string(REPLACE "${UNDEF_BODY}" "    return gatherKind == kGatherUndefined;\n}" m "${gpolicy}")
check_helper("${m}" "${all_sources}" r)
expect_reason("the helper without the verb class" "${r}" "^helper:")
string(REPLACE "kGatherUndefined = 3;" "kGatherUndefined = 2;" m "${gpolicy}")
check_helper("${m}" "${all_sources}" r)
expect_reason("kind 3 renumbered" "${r}" "kGatherUndefined = 3 0 times")
check_helper("${gpolicy}" "${all_sources}\ninline bool GatherKindUndefined(int);" r)
expect_reason("a second helper" "${r}" "defined 2 times")

# (2) the count gone, a second count, the count outside HandleMail.
string(REPLACE "${COUNT_K3}" "" m "${group_cpp}")
check_count("${m}" "${all_sources}" r)
expect_reason("the count gone from HandleMail" "${r}" "in HandleMail: -1")
check_count("${group_cpp}" "${all_sources}\n    CountGrp(guidLow, GrpField::GatherKind3);" r)
expect_reason("a second count" "${r}" "GatherKind3 appears 2 times")
string(REPLACE "if (GatherKindUndefined(v, d.gatherKind))\n                CountGrp" "if (true)\n                CountGrp" m "${group_cpp}")
check_count("${m}" "${all_sources}" r)
expect_reason("the count without the guard" "${r}" "in HandleMail: -1")

# (3) an ack on kind 3, a kind 3 case in ChooseMove, a literal compare.
check_inert("${all_sources}\n    if (d.gatherKind == kGatherUndefined)\n        return AckNo(1);" "${gpolicy}" r)
expect_reason("an ack branching on kind 3" "${r}" "appears 3 times")
string(REPLACE "${DEF_CHOOSE}" "${DEF_CHOOSE}\n    if (gatherKind == kGatherUndefined)\n        return MoveKind::PointGo;" m "${gpolicy}")
check_inert("${all_sources}" "${m}" r)
expect_reason("a kind 3 case in ChooseMove" "${r}" "kind 3 case in ChooseMove: [0-9]")
check_inert("${all_sources}\n    if (gatherKind_ == 3)\n        return;" "${gpolicy}" r)
expect_reason("a literal kind 3 compare" "${r}" "literal kind 3 compare: 'Kind_ == 3")

# (4) the field not last, the name gone, the mirror gone.
string(REPLACE "    HandlerOff,\n    GatherKind3,\n    Count" "    GatherKind3,\n    HandlerOff,\n    Count" m "${policy}")
check_field("${m}" "${shadow_h}" "${shadow_cpp}" r)
expect_reason("gather_kind3 not the last field" "${r}" "lacks the last field")
string(REPLACE "\"handler_off\", \"gather_kind3\"};" "\"handler_off\"};" m "${policy}")
check_field("${m}" "${shadow_h}" "${shadow_cpp}" r)
expect_reason("the name gone" "${r}" "lacks the last field")
string(REPLACE "A3_GRP_SAME(GatherKind3);" "" m "${shadow_cpp}")
check_field("${policy}" "${shadow_h}" "${m}" r)
expect_reason("the shadow assert gone" "${r}" "A3_GRP_SAME 0 times")

# (5) the unit test not called.
string(REPLACE "    TestGatherKind3();\n" "" m "${unit_cpp}")
check_unit("${m}" r)
expect_reason("the unit test not called" "${r}" "called 0 times")

message(STATUS "A3S3_T02G_GATHER_KIND3_SOURCE_CONTRACT=PASS")
