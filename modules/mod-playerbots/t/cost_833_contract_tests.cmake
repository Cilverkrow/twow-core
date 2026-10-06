# Hotfix 8.33 (twow-repo#544): cost side for productive bots.
# 1. "need for quest" answers from one shared sorted list per bot ("needed quest entries"),
#    no quest/destination walk per asked entry.
# 2. AiPlayerbot.RosterFarMove (default 0): roster bots without a player nearby move without the
#    detailed path finding.
file(READ "${PB_SOURCE_DIR}/strategy/values/QuestValues.cpp" quest)
file(READ "${PB_SOURCE_DIR}/strategy/values/ValueContext.h" context)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
foreach (pair
    "quest|return std::binary_search(needed.begin(), needed.end(), entry);"
    "quest|std::vector<int32> NeededQuestEntriesValue::Calculate()"
    "context|creators[\"needed quest entries\"]"
    "ai_cpp|if (activityType == DETAILED_MOVE_ACTIVITY && sPlayerbotAIConfig.rosterFarMove && !HasRealPlayerMaster() &&"
    "ai_cpp|!HasPlayerNearby(WorldPosition(bot).getVisibilityDistance() + sPlayerbotAIConfig.reactDistance))"
    "config_cpp|AiPlayerbot.RosterFarMove\", false"
    "config_dist|AiPlayerbot.RosterFarMove = 0")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "hotfix 8.33: missing ${needle}")
  endif()
endforeach()
# NeedForQuestValue itself must no longer walk the destinations per entry.
string(FIND "${quest}" "bool NeedForQuestValue::Calculate()" nfq)
string(FIND "${quest}" "std::vector<int32> NeededQuestEntriesValue::Calculate()" idx)
string(SUBSTRING "${quest}" ${nfq} -1 nfq_tail)
string(FIND "${nfq_tail}" "if (entry == destination->GetEntry())" old_walk)
if (NOT old_walk EQUAL -1 OR idx LESS nfq)
  message(FATAL_ERROR "hotfix 8.33: need for quest still walks destinations per entry")
endif()
# The far-move gate comes before the priority evaluation (cheap early return).
string(FIND "${ai_cpp}" "sPlayerbotAIConfig.rosterFarMove && !HasRealPlayerMaster()" gate)
string(FIND "${ai_cpp}" "bool PlayerbotAI::AllowActive(ActivityType activityType)" fn)
string(SUBSTRING "${ai_cpp}" ${fn} -1 fn_tail)
string(FIND "${fn_tail}" "ActivePiorityType type = GetPriorityType();" prio)
string(FIND "${fn_tail}" "sPlayerbotAIConfig.rosterFarMove && !HasRealPlayerMaster()" gate_in_fn)
if (gate_in_fn EQUAL -1 OR gate_in_fn GREATER prio)
  message(FATAL_ERROR "hotfix 8.33: far-move gate must open AllowActive")
endif()
# Hotfix 8.33a: the far move skips the route (ResolveMovePath) - it is decided before it in MoveTo2.
file(READ "${PB_SOURCE_DIR}/strategy/actions/MovementActions.cpp" move)
string(FIND "${move}" "if (!detailedMove && sPlayerbotAIConfig.rosterFarMove && endPos.getMapId() == bot->GetMapId() && !bot->GetTransport())" far)
string(FIND "${move}" "[FarMove] started=%u retargeted=%u arrived=%u watched=%u arrive_alt=%u arrive_walk=%u" farlog)
string(FIND "${move}" "TravelPath movePath = ResolveMovePath(startPos, endPos, mover, lastMove);" resolve)
if (far EQUAL -1 OR farlog EQUAL -1 OR resolve EQUAL -1 OR far GREATER resolve)
  message(FATAL_ERROR "hotfix 8.33a: far move must be decided before the route is resolved")
endif()
# Hotfix 8.33b: a pending far move survives other moves; only arrival or another far target end it.
file(READ "${PB_SOURCE_DIR}/strategy/values/LastMovementValue.h" lastmove)
string(FIND "${lastmove}" "void clear()" clear_at)
string(SUBSTRING "${lastmove}" ${clear_at} 400 clear_body)
string(FIND "${clear_body}" "farMoveAt" clear_resets)
string(FIND "${move}" "    lastMove.farMoveAt = 0;

    WorldPosition startPos(bot);" unconditional_reset)
if (NOT clear_resets EQUAL -1 OR NOT unconditional_reset EQUAL -1)
  message(FATAL_ERROR "hotfix 8.33b: other moves must not cancel a pending far move")
endif()
# Hotfix 8.36: the far move ends at a checked arrival point and a real player near the route keeps it walking.
foreach (needle
    "bool FarMoveRouteWatched(WorldPosition const& from, WorldPosition const& to)"
    "!(sPlayerbotAIConfig.rosterFarMoveWatchRoute && FarMoveRouteWatched(farStart, endPos))"
    "bool found = FarMoveArrivalPoint(bot, farStart, endPos, sPlayerbotAIConfig.rosterFarMoveArriveBackYards, arrive);"
    "WorldPosition const arrive = lastMove.farMoveArriveSet ? lastMove.farMoveArrive : endPos;"
    "if (!point.ClosestCorrectPoint(20.0f, 50.0f, bot->GetInstanceId()))")
  string(FIND "${move}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "hotfix 8.36: missing ${needle}")
  endif()
endforeach()
string(FIND "${move}" "return bot->TeleportTo(endPos.getMapId(), endPos.getX(), endPos.getY(), endPos.getZ(), farStart.getAngleTo(endPos));" to_target)
if (NOT to_target EQUAL -1)
  message(FATAL_ERROR "hotfix 8.36: the far move must not end on the target itself")
endif()
message(STATUS "COST_833_CONTRACT=PASS")
