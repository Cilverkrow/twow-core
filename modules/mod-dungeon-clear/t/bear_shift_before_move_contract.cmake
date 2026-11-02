# CLI-GRP 02.11.2026 (g2/CAUSE-DRUIDE-SHIFT-ABBRUCH, OB-00 go; Refs twow-repo#629): the druid tank's pre-pull Bear Form
# shift only PREPARES in its tick, and the same tick's approach (MovementAction::MoveTo / DC SplinePath) interrupts it
# (SPELL_FAILED_INTERRUPTED 0x23, 54x in 12 probe runs). DungeonClear.BearShiftBeforeMove (default 0): 1 = the caller
# holds the approach one tick after a shift, at most once per window (rule pure: DungeonClearMath::ShouldHoldForBearShift,
# gtest TestDungeonClearMath). This pins the wiring as source text: the key, the three call sites through the helper, the
# switch-0 path as the old fire-and-forget call (no hold, no state write, no log), and the helper staying DC-only.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

set(PB_DIR "${DC_SOURCE_DIR}/../mod-playerbots")

function(read_dc path out_var)
  file(READ "${DC_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "bear shift before move: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "bear shift before move: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "bear shift before move: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${offset} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "bear shift before move: ${description}: ${needle} found ${count}x, expected ${expected}")
  endif()
endfunction()

# The text between `first` and `second` (both must exist, in that order).
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "bear shift before move: missing ${first}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "bear shift before move: missing ${second} after ${first}")
  endif()
  string(SUBSTRING "${tail}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_dc("src/Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h" registry)
read_dc("conf/mod_dungeon_clear.conf.dist" conf)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DcTankForm.h" form_h)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DcTankForm.cpp" form)
read_dc("src/Ai/Dungeon/DungeonClear/Action/DcEngageActions.cpp" engage)
read_dc("src/Ai/Dungeon/DungeonClear/Action/DcPullActions.cpp" pull)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DungeonClearMath.h" math_h)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DungeonClearMath.cpp" math)
read_dc("src/Ai/Dungeon/DungeonClear/DcValueKeys.h" keys)
read_dc("src/Ai/Dungeon/DungeonClear/DungeonClearValueContext.h" values)
read_dc("src/Ai/Dungeon/DungeonClear/Value/DungeonClearStateValues.h" state_values)

# Key: registry row (server-only, default 0), documented with 0.
require_text("${registry}" "{ \"BearShiftBeforeMove\",      DcType::Bool,   0,   0,     1,  false }," "registry BearShiftBeforeMove")
require_text("${conf}" "\nDungeonClear.BearShiftBeforeMove = 0\n" "conf BearShiftBeforeMove")
require_text("${conf}" "#    DungeonClear.BearShiftBeforeMove\n" "conf comment block")

# The three call sites go through the helper; none calls EnsureBearForm directly any more.
require_text("${engage}" "    if (distance <= DC_PULL_START_RANGE && DcTankForm::ShiftHoldsApproach(botAI))
        return true;" "walk-in engage through the helper (EngageDirect)")
require_text("${pull}" "    if (phase != DcPullPhase::Idle && DcTankForm::ShiftHoldsApproach(botAI))
        return true;

    switch (phase)" "pull: every non-Idle phase through the helper, before the phase switch")
between("${pull}" "        case DcPullPhase::Idle:" "            // PULL-BACK boss (BossPullbackRegistry)." idle)
require_order("${idle}" "                return false;
            }" "            if (DcTankForm::ShiftHoldsApproach(botAI))
                return true;" "pull Idle: through the helper once the target is resolved")
count_text("${engage}" "DcTankForm::ShiftHoldsApproach(botAI)" 1 "engage call sites")
count_text("${pull}" "DcTankForm::ShiftHoldsApproach(botAI)" 2 "pull call sites")
forbid_text("${engage}" "DcTankForm::EnsureBearForm(" "engage calls EnsureBearForm past the helper")
forbid_text("${pull}" "DcTankForm::EnsureBearForm(" "pull calls EnsureBearForm past the helper")

# The false premise is gone from the comments.
forbid_text("${engage}" "the form is instant and\n    // costs the approach nothing" "old engage premise")
forbid_text("${pull}" "the\n    // shift is instant and doesn't interrupt (or wait on) the approach" "old pull premise")
forbid_text("${form_h}" "(it neither stops a glide nor is interrupted by one)" "old header premise")

# The helper: EnsureBearForm first (the old call), then the switch, and only then the stamp, the guard and the log.
# So the switch-0 path is EnsureBearForm with its result ignored: no hold, no value created or written, no log.
require_text("${form_h}" "    bool ShiftHoldsApproach(PlayerbotAI* botAI);" "helper declaration")
between("${form}" "    bool ShiftHoldsApproach(PlayerbotAI* botAI)\n    {" "        return true;\n    }\n}" helper)
require_order("${helper}" "        if (!EnsureBearForm(botAI))
            return false;" "        if (!DcSettings::GetBool(bot, \"BearShiftBeforeMove\"))
            return false;" "EnsureBearForm before the switch")
require_order("${helper}" "        if (!DcSettings::GetBool(bot, \"BearShiftBeforeMove\"))
            return false;" "GetValue<uint32>(DcKey::BearShiftHold)" "the switch before any state access")
require_order("${helper}" "GetValue<uint32>(DcKey::BearShiftHold)" "DungeonClearMath::ShouldHoldForBearShift(lastHold, getMSTime(),
                                                      DungeonClearMath::BearShiftHoldWindowMs,
                                                      lastHold))
            return false;" "the stall guard on the stamp")
require_order("${helper}" "DungeonClearMath::BearShiftHoldWindowMs" "\"[dungeon-clear] {} holds the approach one tick for {}\"" "the hold line only after the guard")
count_text("${form}" "DcSettings::GetBool(bot, \"BearShiftBeforeMove\")" 1 "the switch read")
count_text("${form}" "DcKey::BearShiftHold" 1 "the stamp access")
count_text("${form}" "holds the approach one tick" 1 "the hold line")

# The guard rule and its window (gtest DungeonClearBearShiftHoldTest pins the timing).
require_text("${math_h}" "    inline constexpr std::uint32_t BearShiftHoldWindowMs = 3000;" "3 s window")
require_text("${math}" "    if (lastHold != 0 && now - lastHold < windowMs)
        return false;" "at most one hold per window")
require_text("${math}" "    lastHoldOut = now != 0 ? now : 1u;
    return true;" "stamp never 0")

# The stamp is its own per-bot value (not a static map, not the pull context), registered once.
require_text("${keys}" "    inline constexpr char const* BearShiftHold           = \"dungeon clear bear shift hold\";" "value key")
require_text("${values}" "        creators[DcKey::BearShiftHold] = &DungeonClearValueContext::dungeon_clear_bear_shift_hold;" "value creator")
require_text("${state_values}" "class DungeonClearBearShiftHoldValue : public ManualSetValue<uint32>" "value class")
file(GLOB_RECURSE dc_sources "${DC_SOURCE_DIR}/src/*.cpp" "${DC_SOURCE_DIR}/src/*.h")
foreach(src IN LISTS dc_sources)
  get_filename_component(name "${src}" NAME)
  if(name MATCHES "^(DcTankForm\\.cpp|DcValueKeys\\.h|DungeonClearValueContext\\.h|DungeonClearStateValues\\.h)$")
    continue()
  endif()
  file(READ "${src}" text)
  string(FIND "${text}" "DcKey::BearShiftHold" hit)
  if(NOT hit EQUAL -1)
    message(FATAL_ERROR "bear shift before move: ${src} touches the hold stamp - only DcTankForm::ShiftHoldsApproach may")
  endif()
endforeach()

# DC-only: no mod-playerbots file references the helper (the fix leaves MoveTo alone - global blast radius).
file(GLOB_RECURSE pb_sources "${PB_DIR}/src/*.cpp" "${PB_DIR}/src/*.h")
list(LENGTH pb_sources pb_count)
if(pb_count EQUAL 0)
  message(FATAL_ERROR "bear shift before move: no mod-playerbots sources under ${PB_DIR}/src")
endif()
foreach(src IN LISTS pb_sources)
  file(READ "${src}" text)
  string(FIND "${text}" "ShiftHoldsApproach" hit)
  if(NOT hit EQUAL -1)
    message(FATAL_ERROR "bear shift before move: ${src} references ShiftHoldsApproach - the fix is DC-only")
  endif()
endforeach()

message(STATUS "bear shift before move contract passed")
