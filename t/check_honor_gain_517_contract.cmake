cmake_policy(VERSION 3.16)

if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#517 stage c (owner decision C0, 16.10.): PlayerScript::OnHonorGain(Player*, float& cp, uint8 type) lets a
# module scale honor - the bot module's AiPlayerbot.BG.BotOnlyHonorRate for bots in a battleground without a real
# player. Neutral seam, no switch: fired once in HonorMgr::Add, right after the global Rate.Honor multiply and only for
# honor that is not a dishonorable kill, before anything is credited (spendable honor, rank points, the honor log), so
# rank points follow the scaled value. The default listener does nothing; without a listener nothing changes.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#517 c: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#517 c: forbidden ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#517 c: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected label)
  string(LENGTH "${needle}" needle_length)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if (offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if (NOT count EQUAL expected)
    message(FATAL_ERROR "#517 c: ${label}: '${needle}' found ${count} times, expected ${expected}")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "#517 c: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if (length EQUAL -1)
    message(FATAL_ERROR "#517 c: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("src/game/ScriptObjects.h" objects_h)
read_source("src/game/ScriptMgr.h" script_h)
read_source("src/game/ScriptMgr.cpp" script_cpp)
read_source("src/game/HonorMgr.cpp" honor_cpp)

# 1. The hook: appended last before the end marker (the values of every older hook stay), a no-op by default.
require_text("${objects_h}" "    PLAYERHOOK_CAN_SWITCH_GUILD,\n    PLAYERHOOK_ON_HONOR_GAIN,\n    PLAYERHOOK_END\n};" "hook appended last, before PLAYERHOOK_END")
count_text("${objects_h}" "PLAYERHOOK_ON_HONOR_GAIN" 1 "one hook value")
region("${objects_h}" "class PlayerScript : public ScriptObject" "class CreatureScript" player_script)
require_text("${player_script}" "virtual void OnHonorGain(Player* /*player*/, float& /*cp*/, uint8 /*type*/) {}" "default listener does nothing")
count_text("${objects_h}" "OnHonorGain(" 1 "one OnHonorGain declaration")

# 2. The Script_* wrapper, like the other module hooks: null guard, every enabled listener, nothing else.
require_text("${script_h}" "void Script_OnHonorGain(Player* player, float& cp, uint8 type);" "wrapper declared")
region("${script_cpp}" "void Script_OnHonorGain(Player* player, float& cp, uint8 type)\n{" "\n}\n" wrapper)
require_text("${wrapper}" "if (!player)\n        return;" "null guard")
require_text("${wrapper}" "ScriptRegistry<PlayerScript>::ForEachEnabledHook(PLAYERHOOK_ON_HONOR_GAIN, [&](PlayerScript* script)\n    {\n        script->OnHonorGain(player, cp, type);\n    });" "every enabled listener, the same cp")
foreach(bad "cp =" "cp *=" "cp /=" "cp +=" "cp -=" "sWorld" "return true" "return false")
  forbid_text("${wrapper}" "${bad}" "wrapper changing cp or deciding itself")
endforeach()
count_text("${script_cpp}" "PLAYERHOOK_ON_HONOR_GAIN" 1 "hook dispatched only by the wrapper")

# 3. The position in HonorMgr::Add: inside the non-dishonorable branch, after the Rate.Honor multiply, before the zero
#    check and before anything is credited.
region("${honor_cpp}" "bool HonorMgr::Add(float cp, uint8 type, Unit* source)\n{" "\nvoid HonorMgr::Update()" add)
set(branch_head "    if (type != DISHONORABLE)\n    {\n        cp *= sWorld.getConfig(CONFIG_FLOAT_RATE_HONOR);\n")
require_text("${add}" "${branch_head}" "Rate.Honor multiply first in the non-dishonorable branch")
region("${add}" "${branch_head}" "\n    }\n" branch)
require_text("${branch}" "        Script_OnHonorGain(m_owner, cp, type);\n        if (cp <= 0.0f)\n            return false;" "hook right before the zero check")
require_order("${add}" "cp *= sWorld.getConfig(CONFIG_FLOAT_RATE_HONOR);" "Script_OnHonorGain(m_owner, cp, type);" "hook after Rate.Honor")
count_text("${add}" "Script_OnHonorGain(" 1 "one hook call in Add")
count_text("${honor_cpp}" "Script_OnHonorGain(" 1 "one hook call in HonorMgr.cpp")
foreach(credit "m_rankPoints += cp * HONOR_TO_RANK_POINTS;" "AddSpendableHonor(uint32(std::max(1.0f, std::ceil(cp))));" "m_honorCP.push_back(honorCP);" "SendPVPCredit(realSource, creditedHonor);")
  require_order("${add}" "Script_OnHonorGain(m_owner, cp, type);" "${credit}" "hook before the credit")
endforeach()
# Dishonorable kills are never offered: the call sits only in the branch above, and the early return for an empty
# record stays first.
require_order("${add}" "if (cp <= 0 || !m_owner)\n        return false;" "${branch_head}" "empty record and missing owner returned before the branch")
forbid_text("${add}" "if (type == DISHONORABLE)\n        Script_OnHonorGain" "hook in the dishonorable path")
require_text("${honor_cpp}" "#include \"ScriptMgr.h\"" "wrapper header included")

# 4. Nobody else calls the wrapper or the hook (core sources only; the Eluna submodule under src/modules is not core).
file(GLOB_RECURSE core_sources "${TW_CORE_ROOT}/src/game/*.cpp" "${TW_CORE_ROOT}/src/game/*.h")
set(seen 0)
foreach(source ${core_sources})
  file(READ "${source}" source_text)
  string(FIND "${source_text}" "Script_OnHonorGain(" at)
  if (at EQUAL -1)
    continue()
  endif()
  math(EXPR seen "${seen} + 1")
  get_filename_component(name "${source}" NAME)
  if (NOT name STREQUAL "HonorMgr.cpp" AND NOT name STREQUAL "ScriptMgr.cpp" AND NOT name STREQUAL "ScriptMgr.h")
    message(FATAL_ERROR "#517 c: Script_OnHonorGain used outside HonorMgr::Add (${source})")
  endif()
endforeach()
if (NOT seen EQUAL 3)
  message(FATAL_ERROR "#517 c: Script_OnHonorGain found in ${seen} core files, expected 3 (declaration, definition, HonorMgr::Add)")
endif()

message(STATUS "honor_gain_517 contract passed")
