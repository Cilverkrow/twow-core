cmake_policy(VERSION 3.16)
# twow-repo#601 (OB-00 28.10.2026, CLI-PATH cli-pathfinding/far-travel-stuck/CAUSE.md, CLI-Deep cand4-B evaluation):
# - Far-move wait = travel (no switch, measurement only): a roster bot on a RosterFarMove (Hotfix 8.33a) stands for the walking
#   time (up to 240 s) and is then moved to its target; [BotActivity] counted that as stuck (cand4-B: 74-87 % of the stuck
#   episodes), so every idle+stuck rule read travel as standing. Now the target's kind; far_wait= on the hour line repeats those
#   seconds, StuckDetail says far_wait=none|overdue, PlayerbotAI::IsFarMovePending() is the shared accessor (CLI-PATH PosStuck),
#   and a death soon after a far-move arrival writes one [FarMoveDeath] line (the cand deaths veto with and without them).
# - AiPlayerbot.ActivityTrace.IdleDetail (default 0): one [IdleDetail] start and end line per idle episode, its own budget.
# Pins the rule (pure, FarMovePolicy.h / BotActivityPolicy.h), the read in the sample, the lines, the key, and that the reports
# change nothing. The rules are tested in t/bot_activity_policy_tests.cpp and t/far_move_policy_tests.cpp.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "far wait / idle detail: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "far wait / idle detail: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "far wait / idle detail: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "far wait / idle detail: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "far wait / idle detail: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "far wait / idle detail: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("BotActivityPolicy.h" policy)
read_source("FarMovePolicy.h" far)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("TravelMgr.h" travel_h)
read_source("strategy/values/LastMovementValue.h" last_move_h)

# 1. The far-move wait state: pure, the grace after its time covers the wait's longest duration (10 s) before Arrive.
require_text("${far}" "constexpr std::uint32_t ArriveGraceSeconds = 15;" "grace after the far move's time")
require_text("${far}" "    if (!pendingAt)\n        return WaitState::None;\n    return now <= pendingAt + ArriveGraceSeconds ? WaitState::Waiting : WaitState::Overdue;" "none / waiting / overdue")
require_text("${last_move_h}" "time_t farMoveAt = 0;" "the far move's time on the last movement value")
require_text("${ai_h}" "#include \"playerbot/FarMovePolicy.h\"" "policy include")
require_text("${ai_h}" "    bool IsFarMovePending() const { return FarMoveWaitState() == ai::far_move::WaitState::Waiting; }" "the shared accessor (CLI-PATH)")
between("${ai_cpp}" "ai::far_move::WaitState PlayerbotAI::FarMoveWaitState() const" "\n}\n" wait_state)
require_text("${wait_state}" "ai::far_move::WaitStateOf(uint64(time(nullptr)),\n        uint64(aiObjectContext->GetValue<LastMovement&>(\"last movement\")->Get().farMoveAt));" "read from the bot's last movement")

# 2. The classifier: a far-move wait in TRAVEL is the target's kind; far_wait= the seconds that were stuck before.
require_text("${policy}" "    bool farMoveWaiting = false;" "state field")
require_text("${policy}" "        if (StuckSample(s) && !s.farMoveWaiting)\n            return Kind::Stuck;" "no stuck while a far move waits")
between("${policy}" "inline bool FarWaitSample(State const& s)" "\n}\n" far_sample)
require_text("${far_sample}" "    if (!s.farMoveWaiting || !StuckSample(s))\n        return false;" "only a waiting, unmoved TRAVEL sample")
require_text("${far_sample}" "    before.farMoveWaiting = false;\n    return Classify(before) == Kind::Stuck;" "stuck without the rule")
require_text("${policy}" "        farWait = 0;" "far_wait per hour")
require_text("${policy}" "    line += \" far_wait=\" + std::to_string(c.farWait);" "far_wait= on the hour line")
require_order("${policy}" "        line += \" quest_log=\" + std::to_string(questLog)" "    line += \" far_wait=\" + std::to_string(c.farWait);" "far_wait= last")

# 3. The sample: the wait state read before the kind, the far-wait seconds credited with the kind.
between("${ai_cpp}" "void PlayerbotAI::SampleActivity(uint32 now)" "\n}\n" sample)
require_text("${sample}" "    ai::far_move::WaitState const farMove = FarMoveWaitState();\n    state.farMoveWaiting = farMove == ai::far_move::WaitState::Waiting;" "the wait state in the sample")
require_order("${sample}" "state.farMoveWaiting = farMove == ai::far_move::WaitState::Waiting;" "ai::bot_activity::Kind const kind = ai::bot_activity::Classify(state);" "read before the kind")
require_text("${sample}" "        if (ai::bot_activity::FarWaitSample(state))\n            activityClock.AddFarWait(credited);" "far_wait credited with the kind")
require_order("${sample}" "activityClock.AddFarWait(credited);" "activityClock.HourDone(now)" "before the hour line")

# 4. StuckDetail: far_wait= at the end of the line.
between("${ai_cpp}" "void PlayerbotAI::ReportStuckDetail(ai::bot_activity::State const& state)" "\n}\n" stuck_report)
require_text("${stuck_report}" "dropped=%u far_wait=%s\"," "far_wait= on the stuck line")
require_text("${stuck_report}" "ai::far_move::WaitStateName(FarMoveWaitState())" "its value")

# 5. The far-move arrival death: one line, within the existing arrive_deaths count, log only.
require_text("${ai_cpp}" "                    far_move::SharedCounters().arriveDeaths.fetch_add(1, std::memory_order_relaxed);\n                    // twow-repo#601 (OB-00 28.10.2026)" "with the arrive_deaths count")
require_text("${ai_cpp}" "sLog.outBasic(\"[FarMoveDeath] bot=%u level=%u map=%u zone=%u x=%.0f y=%.0f after_farmove=1 arrived_s=%u cohort=%c\"," "the arrival death line")

# 6. IdleDetail: the key (default 0), the episode at every sample behind the switch, its own limit, the lines.
require_text("${config_h}" "bool activityTraceIdleDetail = false;" "member default off")
require_text("${config_cpp}" "activityTraceIdleDetail = config.GetBoolDefault(\"AiPlayerbot.ActivityTrace.IdleDetail\", false);" "default 0")
require_text("${config_cpp}" "            {\"ActivityTrace\", Value(activityTrace)},\n            {\"ActivityTrace.IdleDetail\", Value(activityTraceIdleDetail)},\n            {\"ActivityTrace.StuckDetail\", Value(activityTraceStuckDetail)}," "ConfigEcho entry, sorted")
require_text("${conf_dist}" "\nAiPlayerbot.ActivityTrace.IdleDetail = 0\n" "documented")
require_text("${policy}" "constexpr std::uint8_t IdleDetailSamples = 2;" "two idle samples in a row")
require_text("${policy}" "inline StuckDetailLimit& IdleDetailGlobal()\n{\n    static StuckDetailLimit limit;" "its own budget")
forbid_text("${policy}" "mutex" "a lock in the policy")
require_text("${sample}" "    if (sPlayerbotAIConfig.activityTraceIdleDetail)\n    {\n        ai::bot_activity::IdleNote const note = idleEpisode.Note(kind == ai::bot_activity::Kind::Idle, now,\n            ai::bot_activity::IdleWhyOf(state.hasTarget, state.status));\n        if (note != ai::bot_activity::IdleNote::Nothing && ai::bot_activity::IdleDetailGlobal().Take(now))\n            ReportIdleDetail(state, kind, note, now);\n    }" "switch, episode at every sample, limit last")
require_order("${sample}" "ai::bot_activity::Kind const kind = ai::bot_activity::Classify(state);" "ReportIdleDetail(state, kind, note, now);" "after the kind")
require_order("${sample}" "ReportIdleDetail(state, kind, note, now);" "activityClock.Add(kind, now)" "before the credit")
require_text("${sample}" "        state.hasTarget = true;" "a target object at all")
require_text("${ai_h}" "    ai::bot_activity::IdleEpisode idleEpisode;" "per-bot episode")
require_text("${travel_h}" "        bool IsInactiveReselectPending() const { return inactiveReselectPending; }" "read-only reselect flag")
between("${ai_cpp}" "void PlayerbotAI::ReportIdleDetail(" "\n}\n" idle_report)
require_text("${idle_report}" "\"[IdleDetail] state=end bot=%u level=%u idle_s=%u why=%s next=%s dest=%s quest=%u status=%u dropped=%u\"" "end line")
require_text("${idle_report}" "\"[IdleDetail] state=start bot=%u level=%u map=%u zone=%u x=%.0f y=%.0f why=%s dest=%s quest=%u entry=%d status=%u\"" "start line head")
require_text("${idle_report}" "\" status_s=%d left_s=%d reselect=%u purpose=\\\"%s\\\" fetch=%s backoff_s=%d stage=%d free_slots=%u state=%u action=\\\"%s\\\"\"" "start line middle")
require_text("${idle_report}" "fetch->wait_for(std::chrono::seconds(0)) != std::future_status::timeout" "the search state without waiting")
require_text("${idle_report}" "for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)" "20 quest slots, no search")
require_text("${idle_report}" "ai::bot_activity::IdleDetailGlobal().TakeDropped()" "refused lines counted")

# 7. Log only: no behaviour change in the reports or the wait state read.
foreach(region idle_report stuck_report wait_state)
  foreach(change "SetStatus(" "SetExpireIn(" "SetRetry(" "MoveTo(" "->Clear(" "->Set(" "TakeInactiveReselectPending(" "farMoveAt =" ".get()")
    forbid_text("${${region}}" "${change}" "a behaviour change in ${region}")
  endforeach()
endforeach()

message(STATUS "far wait / idle detail source contract passed")
