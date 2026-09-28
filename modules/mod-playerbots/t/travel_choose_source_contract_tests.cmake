function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/values/TravelValues.h" values_h)
file(READ "${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" values_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)

# #416: no map thread waits for a destination job, the choice has a time budget,
# and [BotSlowUpdate] carries the phase.
require_text("${values_h}" "class FutureDestinations" "non-blocking destination future")
require_text("${values_h}" "~FutureDestinations() { Park(); }" "a pending job is parked on destruction")
require_text("${values_cpp}" "parkingLot.Park(std::move(future))" "parking lot used")
require_text("${values_cpp}" "[TravelChoose] state=summary pending=%u parked=%u parked_max=%u cap_hits=%u cap=%u" "summary metrics")
require_text("${ai_cpp}" "FutureDestinations::Collect();" "finished jobs collected from the bot updates")
require_text("${choose}" "ai::travel_choose::OverBudget(WorldTimer::getMSTimeDiffToNow(chooseStart))" "time budget checked")
require_text("${choose}" "chooseResume.Abort(choosePurpose, candidateIndex - 1);" "aborted choice resumes")
require_text("${choose}" "// #416: out of time, not out of targets - the next list goes on." "budget abort is no 'nothing there'")
require_text("${ai_cpp}" "phase=%s since_login_s=%u since_revive_s=%d" "phase in [BotSlowUpdate]")

# Every job start is gated, one gate per Execute that starts jobs.
string(REGEX MATCHALL "if \\(!FutureDestinations::MayStart\\(\\)\\)" gates "${choose}")
list(LENGTH gates gate_count)
if(NOT gate_count EQUAL 3)
  message(FATAL_ERROR "Expected 3 MayStart gates, found ${gate_count}")
endif()

# A parked job may outlive its bot: every async job captures by value only -
# no this, no default capture, no reference.
string(REGEX MATCHALL "std::async\\(std::launch::async" starts "${choose}")
string(REGEX MATCHALL "std::async\\(std::launch::async,[ \t\r\n]*\\[[^]]*\\]" captures "${choose}")
list(LENGTH starts start_count)
list(LENGTH captures capture_count)
if(NOT start_count EQUAL capture_count OR start_count EQUAL 0)
  message(FATAL_ERROR "Every destination job needs an explicit capture list (${capture_count}/${start_count})")
endif()
foreach(capture IN LISTS captures)
  string(REGEX MATCH "this|\\[=|\\[&|, *&|[\r\n\t ]&" bad "${capture}")
  if(bad)
    message(FATAL_ERROR "Destination job captures by reference or this: ${capture}")
  endif()
endforeach()

# 7.2 addition: slowest actions and path builds in [BotSlowUpdate].
file(READ "${PB_SOURCE_DIR}/strategy/Engine.cpp" engine)
require_text("${engine}" "ai->RecordActionTime(action->getName(), executeMs);" "action timing around Execute")
require_text("${ai_cpp}" "slowest_action=" "slowest action in [BotSlowUpdate]")
require_text("${ai_cpp}" "PathFinderStats::builds, PathFinderStats::buildMs" "path builds in [BotSlowUpdate]")
