cmake_policy(VERSION 3.16)
# A3-HARM-UPSTREAM T40 follow-up (OB-00 (a) 16.11.2026): AiPlayerbot.ActivityTrace.StuckReason (log only, default 0)
# - T40 could not say why a stuck bot does not start "attack anything" (aa). With the switch, aa notes where each of its own
#   evaluations returned (isUseful gate, isPossible result, Execute result), the bot stamps its newest non-combat pass, and one
#   [StuckReason] line follows each [StuckDetail] line. Design STUCKREASON-DESIGN-v1.4.md.
# Pins: the key (default 0, echo, dist with the full field list), the store gated by the switch, one body per evaluation (every old
# expression and return unchanged, markers only), the gate order, the pass stamp outside the engine-call pair, the byte-identical
# [StuckDetail] statement, the companion block closed at the end of ReportStuckDetail, reading only caches, own data, the group and
# a same-Map leader, and writing only its own locals (statement-head allowlist, one statement per line, banned token shapes,
# read-callee allowlist), the read-callee allowlist in the two helpers (the peek's three call lines pinned whole), the two record
# members and the bare accessor name counted per file, no comment in the helpers, no raw string, form feed, vertical tab or lone CR
# in the block and the helpers, the pure policy header, the unit test registration and no harm_race_ratchet row for the policy
# header. These are source-text checks of the block and the helpers, not a proof: a macro defined outside the checked regions, an
# implicit conversion or assignment operator behind an allowed head, and a reference obtained outside the block are out of their
# reach (review). Haystacks get \r\n normalised to \n (a lone \r stays and fails in the checked regions); needles with ";" are
# bracket arguments or variables, never list-split.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "stuck reason: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  get_filename_component(PB_MODULE_DIR "${PB_SOURCE_DIR}/../.." ABSOLUTE)
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r\n" "\n" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "stuck reason: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "stuck reason: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "stuck reason: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "stuck reason: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "stuck reason: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

# string(FIND) loop, no MATCHALL (a ";" in the haystack would split a list)
function(count_text text needle out_var)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  string(FIND "${rest}" "${needle}" at)
  while(NOT at EQUAL -1)
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
    string(FIND "${rest}" "${needle}" at)
  endwhile()
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" n)
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "stuck reason: ${description}: '${needle}' found ${n} times, expected ${expected}")
  endif()
endfunction()

# the text between the end of `head` and the start of `tail` (both must exist, head first) is only "    //" comment lines
function(require_only_comments_between text head tail description)
  string(FIND "${text}" "${head}" a)
  string(FIND "${text}" "${tail}" b)
  if(a EQUAL -1 OR b EQUAL -1)
    message(FATAL_ERROR "stuck reason: ${description}: anchors not found")
  endif()
  string(LENGTH "${head}" hl)
  math(EXPR a "${a} + ${hl}")
  if(b LESS a)
    message(FATAL_ERROR "stuck reason: ${description}: anchors out of order")
  endif()
  math(EXPR len "${b} - ${a}")
  string(SUBSTRING "${text}" ${a} ${len} gap)
  if(NOT gap MATCHES "^(    //[^\n]*\n)*$")
    message(FATAL_ERROR "stuck reason: ${description}: only comment lines may stand here, found: ${gap}")
  endif()
endfunction()

# C1 helpers (contracts review v1.1 F1/F2). A region without its whole-line "//" comments.
function(strip_comment_lines text out_var)
  string(REGEX REPLACE "\n[ \t]*//[^\n]*" "" stripped "${text}")
  set(${out_var} "${stripped}" PARENT_SCOPE)
endfunction()

# (e) token shapes that write, or hide a write from the "=" count, without a call: a subscript (std::map operator[] inserts),
# a backslash splice, delete, stream operators, "<=" / ">=" (would hide "x<T>= v"), the digraphs and the alternative tokens
function(c1_forbid_shapes text where)
  foreach(bad "\\" "delete" "<<" ">>" "<=" ">=" "<:" ":>" "<%" "%>" "%:")
    forbid_text("${text}" "${bad}" "C1 shape in the ${where}")
  endforeach()
  string(FIND "${text}" "[" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "stuck reason: forbidden C1 shape in the ${where}: a subscript '['")
  endif()
  string(FIND "${text}" "]" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "stuck reason: forbidden C1 shape in the ${where}: a subscript ']'")
  endif()
  set(alt "(and_eq|and|bitand|bitor|compl|not_eq|not|or_eq|or|xor_eq|xor)")
  if(text MATCHES "(^|[^A-Za-z0-9_])${alt}([^A-Za-z0-9_]|$)")
    string(REGEX MATCH "(^|[^A-Za-z0-9_])${alt}([^A-Za-z0-9_]|$)" hit "${text}")
    message(FATAL_ERROR "stuck reason: forbidden C1 shape in the ${where}: an alternative token: '${hit}'")
  endif()
endfunction()

# (d) after the allowed call sites are blanked, no "name(" / ">(" / ")(" / "}(" / "](" may remain (blanks, tabs or a line break
# before the "(" included)
function(c1_no_other_call text where)
  if(text MATCHES "[A-Za-z0-9_>)}][ \t\n]*\\(" OR text MATCHES "\\][ \t\n]*\\(")
    string(REGEX MATCH "[A-Za-z0-9_:.>-]*[A-Za-z0-9_>)}][ \t\n]*\\(" bad_call "${text}")
    if(bad_call STREQUAL "")
      string(REGEX MATCH "[^\n]*\\][ \t\n]*\\(" bad_call "${text}")
    endif()
    message(FATAL_ERROR "stuck reason: C1 callee: a call outside the named read callees in the ${where}: ${bad_call}")
  endif()
endfunction()

# (c) write tokens
function(c1_forbid_write_tokens text where)
  foreach(bad "->Set" "Set(" "Remove" "Teleport" "Clear(" "Reset(" "++" "--" "+=" "-=" "*=" "/=" "%=" "|=" "&=" "^=" "<<=" ">>="
      "delete " "new " "const_cast" "mutable" "operator" "std::move" "swap(")
    forbid_text("${text}" "${bad}" "C1 write token in the ${where}")
  endforeach()
endfunction()

# (f) text the compiler reads differently from this check (contracts review v1.2 F3/F5): a form feed or vertical tab (C++ blanks
# between a callee and its "("), a CR left after the \r\n normalisation (g++ ends a "//" comment there), a raw string (its lines
# look like comments here)
string(ASCII 12 c1_ff)
string(ASCII 11 c1_vt)
function(c1_forbid_hidden text where)
  string(FIND "${text}" "${c1_ff}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "stuck reason: forbidden C1 byte in the ${where}: a form feed")
  endif()
  string(FIND "${text}" "${c1_vt}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "stuck reason: forbidden C1 byte in the ${where}: a vertical tab")
  endif()
  string(FIND "${text}" "\r" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "stuck reason: forbidden C1 byte in the ${where}: a CR not followed by a line feed")
  endif()
  forbid_text("${text}" "R\"" "C1 raw string in the ${where}")
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("StuckReasonPolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/actions/ChooseTargetActions.cpp" cta)
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
string(REPLACE "\r" "" tests_cmake "${tests_cmake}")
file(READ "${PB_MODULE_DIR}/t/contracts.d/harm_race_ratchet_baseline.txt" ratchet)
string(REPLACE "\r" "" ratchet "${ratchet}")

# ---------------------------------------------------------------------------------------------------------------------------
# 1. Key: neutral default, loaded, echoed in byte order between StuckDetail and BotCheats, documented; the member is read only
#    by the slot (ChooseTargetActions.cpp) and by the stamp and the block (PlayerbotAI.cpp).
require_text("${config_h}" "bool activityTraceStuckReason = false;" "member default off")
require_text("${config_cpp}" "activityTraceStuckReason = config.GetBoolDefault(\"AiPlayerbot.ActivityTrace.StuckReason\", false);" "default 0")
set(echo_window [==[
            {"ActivityTrace.StuckDetail", Value(activityTraceStuckDetail)},
            {"ActivityTrace.StuckReason", Value(activityTraceStuckReason)},
            {"BotCheats", Value(botCheatMask)},
]==])
require_text("${config_cpp}" "${echo_window}" "ConfigEcho entry in byte order")
require_count("${config_cpp}" "{\"ActivityTrace.StuckReason\", Value(activityTraceStuckReason)}," 1 "one ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.ActivityTrace.StuckReason = 0\n" "documented default")
require_order("${conf_dist}" "\nAiPlayerbot.ActivityTrace.StuckDetail = 0\n" "\nAiPlayerbot.ActivityTrace.StuckReason = 0\n" "dist after StuckDetail")

file(GLOB_RECURSE pb_sources RELATIVE "${PB_SOURCE_DIR}" "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(expect_member_PlayerbotAIConfig.h 1)
set(expect_member_PlayerbotAIConfig.cpp 2)
set(expect_member_PlayerbotAI.cpp 2)
set(expect_member_strategy/actions/ChooseTargetActions.cpp 1)
set(expect_accessor_PlayerbotAI.h 1)
set(expect_accessor_strategy/actions/ChooseTargetActions.cpp 1)
set(expect_include_PlayerbotAI.h 1)
# the two records by their bare member names (contracts review v1.1 F2): declarations and accessor in PlayerbotAI.h, the block's
# reads (aa 3, pass 4) and the one pass stamp in PlayerbotAI.cpp; any further use anywhere (a write too) changes a count
set(expect_aa_member_PlayerbotAI.h 2)
set(expect_aa_member_PlayerbotAI.cpp 3)
set(expect_pass_member_PlayerbotAI.h 1)
set(expect_pass_member_PlayerbotAI.cpp 5)
# the bare accessor name (contracts review v1.2 F2): "StuckReasonAa ()" or "&PlayerbotAI::StuckReasonAa" anywhere changes a count
set(expect_accessor_bare_PlayerbotAI.h 1)
set(expect_accessor_bare_strategy/actions/ChooseTargetActions.cpp 1)
foreach(src IN LISTS pb_sources)
  file(READ "${PB_SOURCE_DIR}/${src}" body)
  foreach(kind member accessor include aa_member pass_member accessor_bare)
    if(kind STREQUAL "member")
      set(needle "activityTraceStuckReason")
    elseif(kind STREQUAL "accessor")
      set(needle "StuckReasonAa()")
    elseif(kind STREQUAL "aa_member")
      set(needle "stuckReasonAa")
    elseif(kind STREQUAL "pass_member")
      set(needle "stuckReasonPass")
    elseif(kind STREQUAL "accessor_bare")
      set(needle "StuckReasonAa")
    else()
      set(needle "StuckReasonPolicy.h\"")
    endif()
    count_text("${body}" "${needle}" n)
    if(DEFINED expect_${kind}_${src})
      set(want ${expect_${kind}_${src}})
    else()
      set(want 0)
    endif()
    if(NOT n EQUAL want)
      message(FATAL_ERROR "stuck reason: '${needle}' in ${src}: ${n}, expected ${want}")
    endif()
  endforeach()
endforeach()
require_text("${ai_h}" "#include \"playerbot/StuckReasonPolicy.h\"" "the policy reaches the AI through PlayerbotAI.h")

# ---------------------------------------------------------------------------------------------------------------------------
# 2. ChooseTargetActions.cpp: the store is gated by the switch; one body per evaluation, every old expression and return unchanged.
between("${cta}" "static ai::stuck_reason::AaSlot StuckReasonSlot(PlayerbotAI* ai)\n{" "\n}\n" slot)
require_order("${slot}" "    if (!sPlayerbotAIConfig.activityTraceStuckReason)\n        return ai::stuck_reason::AaSlot{ nullptr, 0 };"
  "&ai->StuckReasonAa()" "the switch before the record")
require_count("${slot}" "return " 2 "two returns in the slot")
require_count("${cta}" "StuckReasonSlot(ai)" 3 "one slot per evaluation")
require_count("${cta}" "ai::stuck_reason::AaNote stuckNote(" 3 "one note per evaluation")
require_count("${cta}" "StuckReasonAa()" 1 "the record only through the slot")
require_order("${cta}" "static ai::stuck_reason::AaSlot StuckReasonSlot(PlayerbotAI* ai)" "bool DpsAssistAction::isUseful()" "slot after the includes")

between("${cta}" "bool AttackAnythingAction::isUseful()\n{" "\n}\n" useful)
between("${cta}" "bool ai::AttackAnythingAction::isPossible()\n{" "\n}\n" possible)
between("${cta}" "bool ai::AttackAnythingAction::Execute(Event& event)\n{" "\n}\n" execute)

set(forbidden_in_notes "AI_VALUE" "GetValue<" "Reset(" "RESET_AI_VALUE" "->Set(" "Set(" "GetTarget(" "AllowActivity(" "HasRealPlayerMaster(" "Calculate(")
foreach(bad IN LISTS forbidden_in_notes)
  forbid_text("${slot}" "${bad}" "a value read or write in the slot")
endforeach()
# the slot body without its signature (contracts review v1.1 F2): no "=", the two brace inits only, no write token or shape, and
# every call one of its read call sites (the exact if head blanked as a whole)
string(LENGTH "static ai::stuck_reason::AaSlot StuckReasonSlot(PlayerbotAI* ai)\n{" slot_head_len)
string(SUBSTRING "${slot}" ${slot_head_len} -1 slot_body)
strip_comment_lines("${slot_body}" slot_code)
require_count("${slot_code}" "=" 0 "no assignment in the slot")
require_count("${slot_code}" "{" 2 "the two slot brace inits only")
require_count("${slot_code}" "}" 2 "the two slot brace inits only")
c1_forbid_write_tokens("${slot_code}" "slot")
c1_forbid_shapes("${slot_code}" "slot")
# no comment of any kind in the slot body, no hidden text (contracts review v1.2 F3-F5)
forbid_text("${slot_body}" "//" "C1 comment in the slot")
forbid_text("${slot_body}" "/*" "C1 comment in the slot")
c1_forbid_hidden("${slot_body}" "slot")
set(slot_calls "${slot_code}")
foreach(site "    if (!sPlayerbotAIConfig.activityTraceStuckReason)" "&ai->StuckReasonAa(" "WorldTimer::getMSTime(")
  string(REPLACE "${site}" " @ " slot_calls "${slot_calls}")
endforeach()
c1_no_other_call("${slot_calls}" "slot")

# every "stuckNote." line in the three bodies is one of the allowed marker forms (no expression, no call)
set(note_useful_ternary "stuckNote.gate = target ? ai::stuck_reason::AaGate::Unsafe : ai::stuck_reason::AaGate::NoTarget;")
set(note_possible_ternary "stuckNote.gate = possible ? ai::stuck_reason::AaGate::Ok : ai::stuck_reason::AaGate::NotPossible;")
set(note_result_ternary "stuckNote.gate = result ? ai::stuck_reason::AaGate::Ok : ai::stuck_reason::AaGate::Failed;")
foreach(body_name useful possible execute)
  set(body "${${body_name}}")
  count_text("${body}" "stuckNote." all_notes)
  count_text("${body}" "\n    stuckNote.gate = " line_notes)
  if(NOT all_notes EQUAL line_notes)
    message(FATAL_ERROR "stuck reason: ${body_name}: every 'stuckNote.' must start a line '    stuckNote.gate = ' (${all_notes} vs ${line_notes})")
  endif()
  set(rest "${body}")
  string(FIND "${rest}" "\n    stuckNote.gate = " at)
  while(NOT at EQUAL -1)
    math(EXPR at "${at} + 5")
    string(SUBSTRING "${rest}" ${at} -1 rest)
    string(FIND "${rest}" "\n" eol)
    string(SUBSTRING "${rest}" 0 ${eol} note_line)
    foreach(bad IN LISTS forbidden_in_notes)
      forbid_text("${note_line}" "${bad}" "a value read or write in a marker")
    endforeach()
    if(NOT note_line MATCHES "^stuckNote\\.gate = ai::stuck_reason::AaGate::[A-Za-z]+;$" AND
       NOT note_line STREQUAL note_useful_ternary AND NOT note_line STREQUAL note_possible_ternary AND
       NOT note_line STREQUAL note_result_ternary)
      message(FATAL_ERROR "stuck reason: ${body_name}: marker line not in an allowed form: ${note_line}")
    endif()
    string(FIND "${rest}" "\n    stuckNote.gate = " at)
  endwhile()
endforeach()
require_count("${useful}" "\n    stuckNote.gate = " 8 "eight isUseful markers")
require_count("${possible}" "\n    stuckNote.gate = " 1 "one isPossible marker")
require_count("${execute}" "\n    stuckNote.gate = " 1 "one Execute marker")

# equivalence counts (design §3.3, review F0: AI_VALUE / GetTarget() only in the isUseful and isPossible regions, so the Execute
# gapTarget removal of open #873/#874 needs no pin edit)
foreach(pair
    "HasRealPlayerMaster()" "AllowActivity(GRIND_ACTIVITY)" "\"can move around\"" "HealerMayAttack(ai)" "MayPull(ai, target)"
    "\"travel target traveling\"")
  require_count("${cta}" "${pair}" 1 "file count unchanged")
  require_count("${useful}" "${pair}" 1 "isUseful count unchanged")
endforeach()
require_count("${cta}" "AttackAction::isPossible()" 1 "file count unchanged")
require_count("${possible}" "AttackAction::isPossible()" 1 "isPossible count unchanged")
require_count("${useful}" "AI_VALUE" 4 "isUseful value reads unchanged")
require_count("${useful}" "GetTarget()" 1 "isUseful target read unchanged")
require_count("${possible}" "AI_VALUE" 0 "isPossible value reads unchanged")
require_count("${possible}" "GetTarget()" 1 "isPossible target read unchanged")
require_count("${useful}" "return false;" 8 "isUseful false returns unchanged")
require_count("${useful}" "return true;" 2 "isUseful true returns unchanged")
require_count("${useful}" "return " 10 "isUseful returns only the literals")

# the note is the first statement (only comment lines before it)
set(note_useful "    ai::stuck_reason::AaNote stuckNote(StuckReasonSlot(ai), ai::stuck_reason::AaStage::Useful, ai::stuck_reason::AaGate::Activity, relevance);\n    if (!ai->AllowActivity(GRIND_ACTIVITY)) //Bot not allowed to be active\n        return false;\n")
set(note_possible "    ai::stuck_reason::AaNote stuckNote(StuckReasonSlot(ai), ai::stuck_reason::AaStage::Possible, ai::stuck_reason::AaGate::NotPossible, relevance);\n    bool const possible = AttackAction::isPossible() && GetTarget();\n")
set(note_execute "    ai::stuck_reason::AaNote stuckNote(StuckReasonSlot(ai), ai::stuck_reason::AaStage::Executed, ai::stuck_reason::AaGate::Failed, relevance);\n    // twow-repo#601 S2 diagnostic")
require_text("${useful}" "${note_useful}" "isUseful note before the activity gate")
require_text("${possible}" "${note_possible}" "isPossible note before the verbatim test")
require_text("${execute}" "${note_execute}" "Execute note before the S2 diagnostic")
require_only_comments_between("${useful}" "bool AttackAnythingAction::isUseful()\n{\n" "${note_useful}" "isUseful: note is the first statement")
require_only_comments_between("${possible}" "bool ai::AttackAnythingAction::isPossible()\n{\n" "${note_possible}" "isPossible: note is the first statement")
string(FIND "${execute}" "    ai::combat_rule::CountPull(ai);" count_pull_at)
string(SUBSTRING "${execute}" ${count_pull_at} -1 after_count_pull)
string(FIND "${after_count_pull}" "\n" count_pull_eol)
math(EXPR count_pull_eol "${count_pull_eol} + 1")
string(SUBSTRING "${after_count_pull}" 0 ${count_pull_eol} count_pull_line)
require_only_comments_between("${execute}" "${count_pull_line}" "${note_execute}" "Execute: note directly after the CountPull line")
require_text("${execute}" "bool ai::AttackAnythingAction::Execute(Event& event)\n{\n    ai::combat_rule::CountPull(ai);" "CountPull stays first")

# isUseful: each marker directly before its gate and after the previous gate's return (the gate order is the release order)
# (one variable per pair: the C++ ";" would split a list)
set(gate_pair_1 "        return false;\n\n    stuckNote.gate = ai::stuck_reason::AaGate::RealMaster;\n    if (ai->HasRealPlayerMaster())\n        return false;\n")
set(gate_pair_2 "        return false;\n\n    stuckNote.gate = ai::stuck_reason::AaGate::CannotMove;\n    if (!AI_VALUE(bool, \"can move around\"))\n        return false;\n\n    Unit* target = GetTarget();\n")
set(gate_pair_3 "    Unit* target = GetTarget();\n    stuckNote.gate = target ? ai::stuck_reason::AaGate::Unsafe : ai::stuck_reason::AaGate::NoTarget;\n\n    if (!target || !ai->IsSafe(target))\n        return false;\n\n    stuckNote.gate = ai::stuck_reason::AaGate::TripNoGrind;\n    // twow-repo#333 Berufe P1-aa")
set(gate_pair_4 "    }\n\n    stuckNote.gate = ai::stuck_reason::AaGate::HealerOrPull;\n    if (!ai::combat_rule::HealerMayAttack(ai))")
set(gate_pair_5 "    if (!ai::combat_rule::MayPull(ai, target))\n        return false;\n\n    stuckNote.gate = ai::stuck_reason::AaGate::OnSight;\n    if(!target->IsPlayer()")
set(gate_pair_6 "        return true;\n\n    stuckNote.gate = ai::stuck_reason::AaGate::Travelling;\n    if (AI_VALUE(bool, \"travel target traveling\")")
set(gate_pair_7 "        return false;\n\n    stuckNote.gate = ai::stuck_reason::AaGate::Ok;\n    return true;")
foreach(pair_var gate_pair_1 gate_pair_2 gate_pair_3 gate_pair_4 gate_pair_5 gate_pair_6 gate_pair_7)
  require_text("${useful}" "${${pair_var}}" "marker adjacent to its gate (${pair_var})")
endforeach()
set(prev_at -1)
foreach(marker RealMaster CannotMove "target ? ai::stuck_reason::AaGate::Unsafe" TripNoGrind HealerOrPull OnSight Travelling Ok)
  if(marker MATCHES "^target")
    set(needle "stuckNote.gate = ${marker}")
  else()
    set(needle "stuckNote.gate = ai::stuck_reason::AaGate::${marker};")
  endif()
  string(FIND "${useful}" "${needle}" at)
  if(at EQUAL -1 OR NOT at GREATER prev_at)
    message(FATAL_ERROR "stuck reason: isUseful marker out of order or missing: ${needle}")
  endif()
  set(prev_at ${at})
endforeach()
set(useful_end "\n    stuckNote.gate = ai::stuck_reason::AaGate::Ok;\n    return true;")
string(LENGTH "${useful}" useful_len)
string(LENGTH "${useful_end}" useful_end_len)
math(EXPR tail_at "${useful_len} - ${useful_end_len}")
string(SUBSTRING "${useful}" ${tail_at} -1 useful_tail)
if(tail_at LESS 0 OR NOT useful_tail STREQUAL useful_end)
  message(FATAL_ERROR "stuck reason: isUseful must end with the Ok marker and 'return true;'")
endif()
# nothing between the healer gate and the MayPull gate (rest_before_pull pins that block)
between("${useful}" "    if (!ai::combat_rule::HealerMayAttack(ai))" "    if (!ai::combat_rule::MayPull(ai, target))" healer_to_pull)
forbid_text("${healer_to_pull}" "stuckNote" "a marker between the healer and the MayPull gate")
forbid_text("${useful}" "stuck_reason::AaGate::None" "the none gate in isUseful")

# isPossible: the verbatim expression in a local, one return of it
require_count("${possible}" "bool const possible = AttackAction::isPossible() && GetTarget();" 1 "the verbatim test")
require_count("${possible}" "return possible;" 1 "the result returned")
require_count("${possible}" "return " 1 "one return")
require_order("${possible}" "${note_possible_ternary}" "return possible;" "marker before the return")

# Execute: the result marker after the needLog block, before the result branch
set(exec_marker [==[
            needLog->stats.picksStale++;
    }
    stuckNote.gate = result ? ai::stuck_reason::AaGate::Ok : ai::stuck_reason::AaGate::Failed;
    if (result)
    {
        Unit* grindTarget = GetTarget();
]==])
require_text("${execute}" "${exec_marker}" "the Execute result marker")
require_order("${execute}" "    bool result = AttackAction::Execute(event);" "${note_result_ternary}" "marker after the attack")
require_count("${execute}" "return result;" 1 "Execute return unchanged")

# ---------------------------------------------------------------------------------------------------------------------------
# 3. PlayerbotAI.cpp: the pass stamp, the byte-identical [StuckDetail] statement, the companion block closed at the function end.
between("${ai_cpp}" "void PlayerbotAI::DoNextAction(bool min)\n{" "\n}\n" next_action)
set(stamp [==[
    bool minimal = !AllowActivity();

    // A3-HARM-UPSTREAM T40 follow-up]==])
require_text("${next_action}" "${stamp}" "the stamp after the minimal decision")
set(stamp_if [==[
    if (sPlayerbotAIConfig.activityTraceStuckReason && currentEngine == engines[(uint8)BotState::BOT_STATE_NON_COMBAT])
        stuckReasonPass = ai::stuck_reason::PassRecord{ WorldTimer::getMSTime(), uint8((minimal || min) ? 1 : 0), 1 };

    SC_PHASE("DoNextAction.engineDoNextAction", bot ? bot->GetName() : "(null)");
    Engine* const engine = currentEngine;
    bool const executed = engine->DoNextAction(NULL, 0, (minimal || min), bot->IsTaxiFlying());
]==])
require_text("${next_action}" "${stamp_if}" "the gated stamp before the engine call")
require_count("${next_action}" "AllowActivity(" 1 "no second AllowActivity")
require_count("${ai_cpp}" "stuckReasonPass = " 1 "one pass store")
require_count("${ai_cpp}" "stuckReasonAa = " 0 "the aa record is written only by the note")

set(stuck_detail_statement [==[
    sLog.outBasic("[StuckDetail] bot=%u level=%u map=%u zone=%u x=%.0f y=%.0f dest=%s quest=%u entry=%d status=%u status_s=%d left_s=%d"
        " dist=%.0f moved=%.1f move_retries=%u motion=%u moving=%u state=%u action=\"%s\" trigger=\"%s\" dropped=%u far_wait=%s"
        " cur_target_entry=%u cur_target_visible=%d",
        bot->GetGUIDLow(), bot->GetLevel(), bot->GetMapId(), bot->GetZoneId(), bot->GetPositionX(), bot->GetPositionY(), dest,
        questId, entry, uint32(state.status), statusSeconds, leftSeconds, distance, state.moved, retries,
        uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()), bot->IsMoving() ? 1u : 0u, uint32(currentState),
        pick.action.c_str(), pick.trigger.c_str(), ai::bot_activity::StuckDetailGlobal().TakeDropped(),
        ai::far_move::WaitStateName(FarMoveWaitState()), curTargetGuid.GetEntry(), curTargetVisible);
]==])
require_count("${ai_cpp}" "${stuck_detail_statement}" 1 "the [StuckDetail] statement byte-identical")

set(block_head [==[
        ai::far_move::WaitStateName(FarMoveWaitState()), curTargetGuid.GetEntry(), curTargetVisible);
    // A3-HARM-UPSTREAM T40 follow-up]==])
require_count("${ai_cpp}" "${block_head}" 1 "the block directly after the [StuckDetail] statement")
set(block_open [==[
    // Off: nothing is read or written.
    if (sPlayerbotAIConfig.activityTraceStuckReason)
    {
]==])
require_only_comments_between("${ai_cpp}" "        ai::far_move::WaitStateName(FarMoveWaitState()), curTargetGuid.GetEntry(), curTargetVisible);\n"
  "${block_open}" "only the block comment between the statement and the block")
require_count("${ai_cpp}" "${block_open}" 1 "one block")

# the block region, cut from the full file text (the stuck_detail region ends before the function's closing brace): from the block
# open to the first "\n    }\n}\n"; that closing brace must be the one that ends ReportStuckDetail.
string(FIND "${ai_cpp}" "void PlayerbotAI::ReportStuckDetail(ai::bot_activity::State const& state)\n{" report_at)
string(SUBSTRING "${ai_cpp}" ${report_at} -1 report_rest)
string(FIND "${report_rest}" "\n}\n" report_end)
math(EXPR report_end "${report_at} + ${report_end}")
string(FIND "${ai_cpp}" "${block_open}" block_at)
string(SUBSTRING "${ai_cpp}" ${block_at} -1 block_rest)
string(FIND "${block_rest}" "\n    }\n}\n" block_len)
if(block_len EQUAL -1)
  message(FATAL_ERROR "stuck reason: block region not closed by '\\n    }\\n}\\n'")
endif()
string(SUBSTRING "${block_rest}" 0 ${block_len} block)
math(EXPR block_end "${block_at} + ${block_len} + 6")
if(report_at EQUAL -1 OR NOT block_at GREATER report_at OR NOT block_end EQUAL report_end)
  message(FATAL_ERROR "stuck reason: the block must close ReportStuckDetail (block end ${block_end}, function end ${report_end})")
endif()

between("${ai_cpp}" "static bool StuckReasonPeek(AiObjectContext* context, std::string const& name, T& out, bool& fresh)\n{" "\n}\n" peek)
require_order("${ai_cpp}" "static bool StuckReasonPeek(AiObjectContext* context" "void PlayerbotAI::ReportStuckDetail(" "peek outside the report")
require_order("${peek}" "if (!context->HasValue(name))" "->LazyGet()" "never creates")
require_order("${peek}" "->Expired(uint32(time(nullptr)))" "->LazyGet()" "never calculates")
require_text("${peek}" "fresh = !untyped->Expired(ai::stuck_reason::FreshSeconds);" "the freshness bit")
forbid_text("${peek}" "->Get()" "a calculating read in the peek")
require_count("${peek}" "return " 4 "the peek returns")
# the peek body without its signature (contracts review v1.1 F2): four "=" (two locals, the two out parameters), no brace, no write
# token or shape, and every call one of its read call sites (the three exact if heads blanked as a whole)
string(LENGTH "static bool StuckReasonPeek(AiObjectContext* context, std::string const& name, T& out, bool& fresh)\n{" peek_head_len)
string(SUBSTRING "${peek}" ${peek_head_len} -1 peek_body)
strip_comment_lines("${peek_body}" peek_code)
require_count("${peek_code}" "=" 4 "only the locals and the out parameters assigned in the peek")
require_count("${peek_code}" "{" 0 "no brace in the peek body")
require_count("${peek_code}" "}" 0 "no brace in the peek body")
c1_forbid_write_tokens("${peek_code}" "peek")
c1_forbid_shapes("${peek_code}" "peek")
# no comment of any kind in the peek body, no hidden text (contracts review v1.2 F3-F5)
forbid_text("${peek_body}" "//" "C1 comment in the peek")
forbid_text("${peek_body}" "/*" "C1 comment in the peek")
c1_forbid_hidden("${peek_body}" "peek")
set(peek_calls "${peek_code}")
# the three call lines pinned whole (contracts review v1.2 F1: GetUntypedValue creates a value for any other name), each blanked
# as a whole line (one variable per line: the C++ ";" would split a list)
set(peek_line_untyped "\n    UntypedValue* const untyped = context->GetUntypedValue(name);\n")
set(peek_line_value "\n    Value<T>* const value = dynamic_cast<Value<T>*>(untyped);\n")
set(peek_line_out "\n    out = value->LazyGet();\n")
foreach(line_var peek_line_untyped peek_line_value peek_line_out)
  require_count("${peek_code}" "${${line_var}}" 1 "the peek call line pinned whole (${line_var})")
  string(REPLACE "${${line_var}}" "\n @ \n" peek_calls "${peek_calls}")
endforeach()
foreach(site "    if (!context->HasValue(name))" "    if (!untyped || untyped->Expired(uint32(time(nullptr))))" "    if (!value)"
    "= !untyped->Expired(")
  string(REPLACE "${site}" " @ " peek_calls "${peek_calls}")
endforeach()
c1_no_other_call("${peek_calls}" "peek")

foreach(region_name block peek)
  set(region "${${region_name}}")
  foreach(bad "AI_VALUE" "SET_AI_VALUE" "RESET_AI_VALUE" "PAI_VALUE" "MAI_VALUE" "GetValue<" "AllowActivity(" "HasRealPlayerMaster("
      "GetBotAI(" "GetGroupMaster(" "GetAction(" "GetTrigger(" "Reset(" "Calculate(" "->Set(" "->Clear(" "Format(" ".get()"
      "GetMapId()" "CanFreeMove" "MapTransDistance" "\"grind target\"" "\"follow target\"" "(\"master target\"" "GetFirstMember("
      "SetStatus(" "SetExpireIn(" "SetRetry(" "MoveTo(" "farMoveAt =" "TakeInactiveReselectPending(")
    forbid_text("${region}" "${bad}" "${region_name}: a decision read or a write")
  endforeach()
endforeach()
require_count("${block}" "->Get()" 1 "one pure Get (curTargetValue)")
require_text("${block}" "curTargetValue && curTargetValue->Get() != nullptr" "the resolved current target")
require_count("${block}" "StuckReasonPeek<" 2 "two peeks")
require_text("${block}" "StuckReasonPeek<bool>(aiObjectContext, \"invalid target::current target\", invalid, invalidFresh)" "peek invalid target")
require_text("${block}" "StuckReasonPeek<bool>(aiObjectContext, \"can free move::wandermax\", cfmMax, cfmFresh)" "peek wandermax")
require_count("${block}" "SameMapObject(bot, leaderPlayer)" 1 "SameMapObject once")
require_order("${block}" "SameMapObject(bot, leaderPlayer)" "GetDistance2dToCenter(leaderPlayer)" "position only under SameMapObject")
require_text("${block}" "line.leaderDist = leaderSameMap ? bot->GetDistance2dToCenter(leaderPlayer) : -1.0f;" "distance only on the same Map object")
# nc_mult (review F1): exactly grind + five names, each on the non-combat engine
require_count("${block}" "HasStrategy(" 6 "grind and the five nc_mult strategies")
require_count("${block}" ", BotState::BOT_STATE_NON_COMBAT)" 6 "all on the non-combat engine")
foreach(name "grind" "passive" "wbuff travel" "dungeon clear" "pull" "stealthed")
  require_count("${block}" "HasStrategy(\"${name}\", BotState::BOT_STATE_NON_COMBAT)" 1 "strategy read")
endforeach()
require_order("${block}" "HasStrategy(\"passive\"" "HasStrategy(\"wbuff travel\"" "nc_mult argument order")
require_order("${block}" "HasStrategy(\"wbuff travel\"" "HasStrategy(\"dungeon clear\"" "nc_mult argument order")
require_order("${block}" "HasStrategy(\"dungeon clear\"" "HasStrategy(\"pull\"" "nc_mult argument order")
require_order("${block}" "HasStrategy(\"pull\"" "HasStrategy(\"stealthed\"" "nc_mult argument order")
require_text("${block}" "line.ncMult = ai::stuck_reason::NcMultOf(" "nc_mult through the policy")
require_count("${block}" "sLog.outBasic(\"%s\", ai::stuck_reason::FormatLine(line).c_str());" 1 "one line, text from the policy")
require_count("${block}" "sLog." 1 "one log statement in the block")
require_count("${ai_cpp}" "ai::stuck_reason::FormatLine(" 1 "the line only in the block")
require_count("${ai_cpp}" "\"[StuckReason]" 0 "the head lives in the policy")
require_count("${block}" "stuckReasonAa" 3 "own aa record read (copy, age, seen)")
require_count("${block}" "stuckReasonPass" 4 "own pass record read (age, seen, minimal)")
# cls= (stealthed verification: nc_mult 16 decodes by class): the bot's own class byte, right after bot=
require_count("${block}" "line.cls = bot->getClass();" 1 "the class from the bot's own data")
require_order("${block}" "line.bot = bot->GetGUIDLow();\n        line.cls = bot->getClass();" "line.aa = stuckReasonAa;" "cls after bot")
require_text("${conf_dist}" "\"[StuckReason] bot= cls= aa_stage=" "dist field list with cls")
# the whole dist field list (contracts review v1.1 F3): all 17 names in the FormatLine order, with the token lists (aa_gate in the
# AaGate enum order); the comment wrap is joined first
between("${conf_dist}" "\"[StuckReason] bot= " "\" - where" dist_fields)
string(REPLACE "\n# " " " dist_fields "${dist_fields}")
string(REPLACE "| " "|" dist_fields "${dist_fields}")
set(dist_fields_want "\"[StuckReason] bot= cls= aa_stage=never|useful|possible|executed aa_gate=none|activity|real_master|cannot_move|no_target|unsafe|trip_no_grind|healer_or_pull|on_sight|travelling|ok|not_possible|failed aa_age_ms= aa_rel= pass_age_ms= pass_min= grind= nc_mult= old_rule=none|unseen|invalid|valid|stale|nc cfm_max= cfm_fresh= leader=none|self|offline|other_map|same_map leader_guid= group_size= leader_dist=")
if(NOT dist_fields STREQUAL dist_fields_want)
  message(FATAL_ERROR "stuck reason: dist field list not the 17 fields in order: ${dist_fields}")
endif()

# C1 (contracts review): the block writes only its own locals. Per line (the stuckNote. loop mechanism): (a) a statement line
# starts with an allowed head; a continuation line is one whose previous code line ends in none of ";" "{" "}" (and is not the
# closing ")" of an if head); no comment after code; (e) no banned token shape (subscript, splice, delete, stream operators,
# "<=" / ">=", digraphs, alternative tokens), at most one ";", "{" and "}" only as a whole line; (b) no "=" besides the head's
# own ("==" and "!=" removed first). Block-wide: (c) no write token; (d) every call is a named read callee (each allowed call
# site is blanked, then no "name(" may remain).
string(LENGTH "${block_open}" block_open_len)
string(SUBSTRING "${block}" ${block_open_len} -1 block_body)
# (f) no hidden text and no "/*" in the block body; "//" only as a whole-line comment without a quote (contracts review v1.2 F3-F5)
c1_forbid_hidden("${block_body}" "block")
forbid_text("${block_body}" "/*" "C1 comment in the block")
set(c1_rest "${block_body}\n")
set(c1_cont 0)
set(c1_in_if 0)
set(c1_lines 0)
string(FIND "${c1_rest}" "\n" c1_eol)
while(NOT c1_eol EQUAL -1)
  string(SUBSTRING "${c1_rest}" 0 ${c1_eol} c1_raw)
  math(EXPR c1_next "${c1_eol} + 1")
  string(SUBSTRING "${c1_rest}" ${c1_next} -1 c1_rest)
  string(FIND "${c1_rest}" "\n" c1_eol)
  string(STRIP "${c1_raw}" c1_line)
  if(c1_line STREQUAL "")
    continue()
  endif()
  string(FIND "${c1_line}" "//" c1_slash)
  if(c1_slash EQUAL 0)
    string(FIND "${c1_line}" "\"" c1_quote)
    if(NOT c1_quote EQUAL -1)
      message(FATAL_ERROR "stuck reason: C1 comment: a quote in a block comment line: ${c1_line}")
    endif()
    continue()
  endif()
  math(EXPR c1_lines "${c1_lines} + 1")
  string(FIND "${c1_line}" "/*" c1_star)
  if(NOT c1_slash EQUAL -1 OR NOT c1_star EQUAL -1)
    message(FATAL_ERROR "stuck reason: C1 head: no comment after code in the block: ${c1_line}")
  endif()
  # (e) per code line (contracts review v1.1 F1)
  c1_forbid_shapes("${c1_line}" "block")
  if(NOT c1_line STREQUAL "{" AND NOT c1_line STREQUAL "}")
    string(FIND "${c1_line}" "{" c1_ob)
    string(FIND "${c1_line}" "}" c1_cb)
    if(NOT c1_ob EQUAL -1 OR NOT c1_cb EQUAL -1)
      message(FATAL_ERROR "stuck reason: C1 shape: a brace only as a whole line in the block: ${c1_line}")
    endif()
  endif()
  string(REGEX REPLACE "[^;]" "" c1_semi "${c1_line}")
  string(LENGTH "${c1_semi}" c1_semi_n)
  if(c1_semi_n GREATER 1)
    message(FATAL_ERROR "stuck reason: C1 shape: more than one ';' on a block line: ${c1_line}")
  endif()
  set(c1_eq_allowed 0)
  if(NOT c1_cont)
    if(c1_line MATCHES "^line\\.[A-Za-z]+ = " OR c1_line MATCHES "^uint32 const [A-Za-z]+ = " OR
       c1_line MATCHES "^bool const [A-Za-z]+ = " OR c1_line MATCHES "^Group\\* const [A-Za-z]+ = " OR
       c1_line MATCHES "^ObjectGuid const [A-Za-z]+ = " OR c1_line MATCHES "^Player\\* const [A-Za-z]+ = ")
      set(c1_eq_allowed 1)
    elseif(c1_line MATCHES "^bool [A-Za-z]+ = false, [A-Za-z]+ = false;$")
      set(c1_eq_allowed 2)
    elseif(c1_line STREQUAL "ai::stuck_reason::Line line;" OR c1_line STREQUAL "{" OR c1_line STREQUAL "}")
      set(c1_eq_allowed 0)
    else()
      string(FIND "${c1_line}" "if (StuckReasonPeek<" c1_if)
      string(FIND "${c1_line}" "sLog.outBasic(\"%s\", " c1_log)
      if(c1_if EQUAL 0)
        set(c1_in_if 1)
      elseif(NOT c1_log EQUAL 0)
        message(FATAL_ERROR "stuck reason: C1 head: block statement not in the allowed heads: ${c1_line}")
      endif()
    endif()
  endif()
  # (b) the "=" signs left after "==" and "!=" are removed ("<=" / ">=" fail at (e)): the head's own only, none on continuation lines
  string(REPLACE "==" "" c1_eq "${c1_line}")
  string(REPLACE "!=" "" c1_eq "${c1_eq}")
  string(REGEX REPLACE "[^=]" "" c1_eq "${c1_eq}")
  string(LENGTH "${c1_eq}" c1_eq_n)
  if(NOT c1_eq_n EQUAL c1_eq_allowed)
    message(FATAL_ERROR "stuck reason: C1 assignment: ${c1_eq_n} '=' on a block line, allowed ${c1_eq_allowed}: ${c1_line}")
  endif()
  # the next line continues this statement unless it ends here
  string(LENGTH "${c1_line}" c1_len)
  math(EXPR c1_last "${c1_len} - 1")
  string(SUBSTRING "${c1_line}" ${c1_last} 1 c1_end)
  if(c1_end STREQUAL ";" OR c1_end STREQUAL "{" OR c1_end STREQUAL "}")
    set(c1_cont 0)
    set(c1_in_if 0)
  elseif(c1_in_if AND c1_end STREQUAL ")")
    set(c1_cont 0)
    set(c1_in_if 0)
  else()
    set(c1_cont 1)
  endif()
endwhile()
if(c1_cont)
  message(FATAL_ERROR "stuck reason: C1 head: the block ends inside a statement")
endif()
if(c1_lines LESS 20)
  message(FATAL_ERROR "stuck reason: C1 head: the line loop saw only ${c1_lines} code lines")
endif()
# (c) write tokens, anywhere in the block body
c1_forbid_write_tokens("${block_body}" "block")
# (d) every call in the block body is one of these read call sites (with their left delimiter, which the blanking keeps); anything
# else that is called fails, also with blanks or a line break before its "(" (only the if head is blanked as a whole); whole-line
# comments are cut first, so a comment line between a callee and its "(" hides nothing (contracts review v1.2 F3)
strip_comment_lines("\n${block_body}" c1_calls)
foreach(site "if (StuckReasonPeek<bool>(" "WorldTimer::getMSTime(" "bot->GetGUIDLow(" "bot->getClass(" "ai::stuck_reason::AgeMs(" "ai::stuck_reason::NcMultOf("
    "ai::stuck_reason::OldRuleOf(" "curTargetGuid.IsEmpty(" "curTargetValue->Get(" "bot->GetGroup(" "group->GetLeaderGuid("
    "bot->GetObjectGuid(" "sObjectMgr.GetPlayer(" "leaderGuid.GetCounter(" "group->GetMembersCount(" "ai::stuck_reason::LeaderOf("
    "bot->GetDistance2dToCenter(" "sLog.outBasic(" "ai::stuck_reason::FormatLine(")
  string(REPLACE "${site}" " @ " c1_calls "${c1_calls}")
endforeach()
foreach(site "int32(" "HasStrategy(\"" "StuckReasonPeek<bool>(" "ObjectGuid(" "SameMapObject(")
  string(REPLACE " ${site}" "  @ " c1_calls "${c1_calls}")
  string(REPLACE "(${site}" "( @ " c1_calls "${c1_calls}")
endforeach()
string(REPLACE ").c_str(" ") @ " c1_calls "${c1_calls}")
c1_no_other_call("${c1_calls}" "block")

# PlayerbotAI.h: the members next to the episode, one public accessor
require_text("${ai_h}" "    ai::stuck_reason::AaRecord stuckReasonAa;\n    ai::stuck_reason::PassRecord stuckReasonPass;\n" "the two records")
require_text("${ai_h}" "    ai::stuck_reason::AaRecord& StuckReasonAa() { return stuckReasonAa; }\n" "the accessor")
require_order("${ai_h}" "ai::stuck_reason::AaRecord& StuckReasonAa()" "protected:\n\tPlayer* bot;" "accessor public")
require_order("${ai_h}" "protected:\n\tPlayer* bot;" "    ai::stuck_reason::AaRecord stuckReasonAa;" "records protected")

# ---------------------------------------------------------------------------------------------------------------------------
# 5. Policy header: pure (no game include, no lock, no log, no clock, no config, no statics), no ratchet shape, the tokens.
foreach(bad "#include \"" "mutex" "sLog" "static " "std::atomic" "WorldTimer" "sPlayerbotAIConfig"
    "GetMapId()" "GetBotAI(" "AI_VALUE" "->GetTradeData()" "getQuestStatusMap")
  forbid_text("${policy}" "${bad}" "in the pure policy header")
endforeach()
require_text("${policy}" "\"[StuckReason] bot=%u cls=%u aa_stage=%s aa_gate=%s aa_age_ms=%d aa_rel=%.2f pass_age_ms=%d pass_min=%d\"" "line head")
require_text("${policy}" "unsigned(line.bot), unsigned(line.cls), StageToken(line.aa.stage)," "cls right after bot")
require_text("${policy}" "    std::uint32_t bot = 0;\n    std::uint32_t cls = 0;\n" "the class field")
require_text("${policy}" "\" grind=%d nc_mult=%u old_rule=%s cfm_max=%d cfm_fresh=%d leader=%s leader_guid=%u group_size=%u leader_dist=%.0f\"" "line tail")
require_text("${policy}" "enum class AaStage : std::uint8_t { Never, Useful, Possible, Executed };" "stages")
require_text("${policy}" "    None, Activity, RealMaster, CannotMove, NoTarget, Unsafe, TripNoGrind, HealerOrPull, OnSight, Travelling, Ok, NotPossible, Failed\n" "gates")
foreach(token "never" "useful" "possible" "executed" "none" "activity" "real_master" "cannot_move" "no_target" "unsafe" "trip_no_grind"
    "healer_or_pull" "on_sight" "travelling" "ok" "not_possible" "failed" "unseen" "nc" "stale" "invalid" "valid" "self" "offline"
    "other_map" "same_map" "passive" "wbuff travel" "dungeon clear" "pull" "stealthed")
  require_text("${policy}" "return \"${token}\";" "token")
endforeach()
foreach(bit "NcMultPassive = 1;" "NcMultWbuffTravel = 2;" "NcMultDungeonClear = 4;" "NcMultPull = 8;" "NcMultStealthed = 16;")
  require_text("${policy}" "constexpr std::uint32_t ${bit}" "nc_mult bit")
endforeach()
require_text("${policy}" "constexpr std::uint32_t FreshSeconds = 5;" "fresh = checked within 5 s")
set(note_dtor [==[
    ~AaNote()
    {
        if (slot.record != nullptr)
        {
            slot.record->ms = slot.ms;
            slot.record->relevance = relevance;
            slot.record->stage = std::uint8_t(stage);
            slot.record->gate = std::uint8_t(gate);
        }
    }
]==])
require_text("${policy}" "${note_dtor}" "the note stores only into a non-null record")
require_text("${policy}" "    AaNote(AaNote const&) = delete;\n    AaNote& operator=(AaNote const&) = delete;\n" "non-copyable note")

# ---------------------------------------------------------------------------------------------------------------------------
# 6. Unit test registered like the other policy tests.
require_text("${tests_cmake}" "add_executable(stuck_reason_policy_tests\n  \"\${PB_MODULE_DIR}/t/stuck_reason_policy_tests.cpp\")" "unit test target")
require_text("${tests_cmake}" "add_test(NAME stuck_reason_policy\n  COMMAND stuck_reason_policy_tests" "unit test registered")

# 7. harm_race_ratchet: no row for the policy header. The rows of the touched files are not pinned here (other changes may move
#    them with -DHARM_RATCHET_UPDATE=ON); the ratchet contract itself proves this change adds or removes no cross-Map access.
forbid_text("${ratchet}" "StuckReasonPolicy.h" "a ratchet row for the policy header")

message(STATUS "stuck reason: all checks passed")
