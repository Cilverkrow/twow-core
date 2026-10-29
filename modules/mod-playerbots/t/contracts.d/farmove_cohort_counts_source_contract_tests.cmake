# twow-repo#601 (CLI-PATH ArriveSafe A/B, OB-00 28.10.2026): the [FarMove] counters are shared by all bots, so an in-realm A/B
# (mode 2 = cohort B, odd guid) could not read arrive_deaths per arrival per cohort. The minute line now carries cohort B's share,
# arrived_b= and arrive_deaths_b= (A = total - b), after arrive_deaths= and before the ArriveSafe suffix. Counted next to the
# totals, no switch, log only. Pins the counters, the two counts, and the line.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "far-move cohort counts: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 FarMove cohort counts: missing ${description}: ${needle}")
  endif()
endfunction()

read_source("FarMovePolicy.h" far)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

require_text("${far}" "    std::atomic<std::uint32_t> arrivedB{0};\n    std::atomic<std::uint32_t> arriveDeathsB{0};" "cohort B counters")
require_text("${move_cpp}" "                counters.arrived.fetch_add(1, std::memory_order_relaxed);\n                if (ai::cohort::InB(bot->GetGUIDLow()))\n                    counters.arrivedB.fetch_add(1, std::memory_order_relaxed);" "B arrival next to the total")
require_text("${ai_cpp}" "            if (ai::cohort::InB(bot->GetGUIDLow()))\n            {\n                LastMovement const& farMove = AI_VALUE(LastMovement&, \"last movement\");\n                if (farMove.farMoveArrivedAt && time(nullptr) - farMove.farMoveArrivedAt <= time_t(far_move::ArriveDeathSeconds))\n                    far_move::SharedCounters().arriveDeathsB.fetch_add(1, std::memory_order_relaxed);" "B arrival death, the same window as arrive_deaths")
require_text("${move_cpp}" "                uint32 const arrivedB = counters.arrivedB.exchange(0, std::memory_order_relaxed);\n                uint32 const arriveDeathsB = counters.arriveDeathsB.exchange(0, std::memory_order_relaxed);" "read once a minute")
require_text("${move_cpp}" "arriveWalk || arriveDeaths || arrivedB || arriveDeathsB)" "a minute with only B counts still logs")
require_text("${move_cpp}" "arrive_deaths=%u arrived_b=%u arrive_deaths_b=%u%s\"," "the fields after arrive_deaths=, before the suffix")
require_text("${move_cpp}" "arriveDeaths, arrivedB, arriveDeathsB, safe.c_str());" "their values")
require_text("${conf_dist}" "arrive_deaths arrived_b arrive_deaths_b (cohort B = odd guid; A = total - b)" "documented")

message(STATUS "far-move cohort counts source contract passed")
