if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#727 (owner 20.11.2026): the Darkmoon Faire state comes only from
# DarkmoonSchedule::At (UTC, Mon setup / Tue-Fri open / Sat teardown / Sun none,
# Mon 30.11.2026 = Mulgore), and DarkmoonFaire wakes the event manager right
# after 00:00 UTC. The old week-of-year/Wednesday logic is gone.

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${label}: ${needle}")
  endif()
endfunction()

file(READ "${TW_CORE_ROOT}/src/game/HardcodedEvents.cpp" cpp)
file(READ "${TW_CORE_ROOT}/src/game/HardcodedEvents.h" h)
file(READ "${TW_CORE_ROOT}/src/game/DarkmoonSchedule.h" sched)

string(FIND "${cpp}" "DarkmoonState DarkmoonFaire::GetDarkmoonState()" b)
string(FIND "${cpp}" "/*\n * Fireworks Show" e)
if (b EQUAL -1 OR e EQUAL -1 OR e LESS b)
  message(FATAL_ERROR "DarkmoonFaire::GetDarkmoonState block not found")
endif()
math(EXPR n "${e} - ${b}")
string(SUBSTRING "${cpp}" ${b} ${n} dmf)

require_text("${h}" "#include \"DarkmoonSchedule.h\"" "schedule include")
require_text("${h}" "uint32 GetNextUpdateDelay() override; // twow-repo#727" "DarkmoonFaire update delay override")
require_text("${dmf}" "DarkmoonSchedule::At(static_cast<int64_t>(time(nullptr)))" "state from the UTC schedule")
require_text("${dmf}" "case DarkmoonSchedule::Phase::Teardown:" "teardown uses the setup event")
require_text("${dmf}" "return DARKMOON_NONE;" "Sunday without a faire")
require_text("${dmf}" "uint32 DarkmoonFaire::GetNextUpdateDelay()" "update delay definition")
require_text("${dmf}" "DarkmoonSchedule::SecondsUntilNextDay(" "delay until next 00:00 UTC")
forbid_text("${dmf}" "localtime" "local time zone in the DMF schedule")
forbid_text("${dmf}" "tm_yday" "week of the year parity")
forbid_text("${dmf}" "tm_wday == 3" "Wednesday setup")

require_text("${sched}" "constexpr int64_t ReferenceMondayDay = 20787;" "reference Monday 30.11.2026")
forbid_text("${sched}" "#include \"" "project includes in the dependency-free schedule")

message(STATUS "DARKMOON_SCHEDULE_727_CONTRACT=PASS")
