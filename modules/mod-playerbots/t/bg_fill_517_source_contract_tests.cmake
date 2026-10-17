# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#517 stage a: behind AiPlayerbot.BG.Enable && AiPlayerbot.BG.PlayerFill (both default 0) the bot manager
# fills a battleground a real player queues for (WSG/AB/AV) with persistent-roster bots, from the world thread:
# RandomPlayerbotMgr::CheckBgFill (roster branch of UpdateAIInternal), accept through BGStatusAction::AcceptInvite,
# return to questing through PlayerbotAI::OnReturnFromBattleground. Pure rules: t/bg_fill_policy_tests.cpp.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#517: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#517: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#517: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_length)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#517: ${description}: '${needle}' found ${count} times, expected ${expected}")
  endif()
endfunction()

# Every occurrence of needle has guard_a or guard_b within the window characters before it.
function(require_guarded text needle guard_a guard_b window description)
  string(LENGTH "${needle}" needle_length)
  set(seen 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR seen "${seen} + 1")
    set(from 0)
    set(length ${offset})
    if(offset GREATER window)
      math(EXPR from "${offset} - ${window}")
      set(length ${window})
    endif()
    string(SUBSTRING "${rest}" ${from} ${length} before)
    string(FIND "${before}" "${guard_a}" guard_a_offset)
    string(FIND "${before}" "${guard_b}" guard_b_offset)
    if(guard_a_offset EQUAL -1 AND guard_b_offset EQUAL -1)
      string(SUBSTRING "${rest}" ${offset} 80 line)
      message(FATAL_ERROR "#517: ${description}: no '${guard_a}' or '${guard_b}' before: ${line}")
    endif()
    math(EXPR next "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(seen EQUAL 0)
    message(FATAL_ERROR "#517: ${description}: '${needle}' not found")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#517: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#517: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("RandomPlayerbotMgr.h" mgr_h)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("BgFillPolicy.h" policy_h)
read_source("strategy/actions/BattleGroundJoinAction.h" join_h)
read_source("strategy/actions/BattleGroundJoinAction.cpp" join_cpp)

# 1. Keys: every one default 0/false in code and 0 in the dist, restart-only, never shipped on.
require_text("${config_h}" "bool bgEnable = false;" "BG.Enable member default off")
require_text("${config_h}" "bool bgPlayerFill = false;" "BG.PlayerFill member default off")
require_text("${config_h}" "uint32 bgMaxConcurrent = 0;" "BG.MaxConcurrent member default 0")
require_text("${config_h}" "uint32 bgCheckIntervalSeconds = 0;" "BG.CheckIntervalSeconds member default 0")
require_text("${config_h}" "bool bgTrace = false;" "BG.Trace member default off")
require_text("${config_cpp}" "bgEnable = config.GetBoolDefault(\"AiPlayerbot.BG.Enable\", false);" "BG.Enable read, default false")
require_text("${config_cpp}" "bgPlayerFill = config.GetBoolDefault(\"AiPlayerbot.BG.PlayerFill\", false);" "BG.PlayerFill read, default false")
# Negative values read as 0: MaxConcurrent -1 must not wrap to 4294967295 (no cap) in the uint32 member.
require_text("${config_cpp}" "bgMaxConcurrent = uint32(std::max<int32>(0, config.GetIntDefault(\"AiPlayerbot.BG.MaxConcurrent\", 0)));" "BG.MaxConcurrent read, default 0, negative = 0")
require_text("${config_cpp}" "bgCheckIntervalSeconds = uint32(std::max<int32>(0, config.GetIntDefault(\"AiPlayerbot.BG.CheckIntervalSeconds\", 0)));" "BG.CheckIntervalSeconds read, default 0, negative = 0")
require_text("${config_cpp}" "bgTrace = config.GetBoolDefault(\"AiPlayerbot.BG.Trace\", false);" "BG.Trace read, default false")
foreach(key Enable PlayerFill MaxConcurrent CheckIntervalSeconds Trace)
  require_text("${conf_dist}" "\nAiPlayerbot.BG.${key} = 0\n" "AiPlayerbot.BG.${key} documented with 0")
  count_text("${conf_dist}" "\nAiPlayerbot.BG.${key} = " 1 "AiPlayerbot.BG.${key} set once in the dist")
endforeach()
require_text("${conf_dist}" "restart-only" "BG keys documented as restart-only")

# 2. Roster branch: LoginFreeBots(), then the gated CheckBgFill, then the roster return; no legacy BG queue count.
region("${mgr_cpp}" "if (sPlayerbotAIConfig.persistentActiveRosterEnabled)\n    {\n        if (!persistentRosterInitialized)" "    ScaleBotActivity();\n    if (sPlayerbotAIConfig.asyncBotLogin)" roster_branch)
# Stage b (twow-repo#517): SlotRelease runs in the same pass and needs no PlayerFill; the master switch gates both.
# Stage c: announced bot-own battlegrounds run in the same pass too, also without PlayerFill, but only behind their full
# gate BgOwnEnabled (Enable && Announce && IntervalMinutes && MaxConcurrent, pinned in bg_announce_517): Announce alone
# opens nothing (review c2, C1 "everything off = no work at all").
require_text("${roster_branch}" "if (sPlayerbotAIConfig.bgEnable && (sPlayerbotAIConfig.bgPlayerFill || sPlayerbotAIConfig.bgSlotRelease || BgOwnEnabled()))\n            CheckBgFill();" "fill gated by bgEnable && (bgPlayerFill || bgSlotRelease || BgOwnEnabled())")
forbid_text("${roster_branch}" "sPlayerbotAIConfig.bgAnnounce" "the pass opened by Announce alone")
require_order("${roster_branch}" "LoginFreeBots();" "CheckBgFill();" "LoginFreeBots before CheckBgFill")
require_order("${roster_branch}" "CheckBgFill();" "PlayerbotHolder::UpdateAIInternal(elapsed, minimal);\n        if (sPlayerbotAIConfig.worldBotsTrace)" "CheckBgFill before the roster return")
forbid_text("${roster_branch}" "CheckBgQueue" "legacy BG queue count in the roster branch (BGJoinAction would queue roster bots)")
count_text("${mgr_cpp}" "CheckBgFill();" 1 "CheckBgFill called only from the gated roster branch")

# 3. The fill path: world thread, bounded, no Refresh, no DB bot list, no all-bot loop, short locks only.
region("${mgr_cpp}" "void RandomPlayerbotMgr::CheckBgFill()" "void RandomPlayerbotMgr::CheckBgQueue()" fill_path)
forbid_text("${fill_path}" "Refresh(" "Refresh on the fill path (free repair, money, consumables)")
forbid_text("${fill_path}" "GetBgBots(" "synchronous DB bot list on the fill path")
forbid_text("${fill_path}" "ForEachPlayerbot(" "unbounded loop over all bots on the fill path")
forbid_text("${fill_path}" "shared_mutex" "shared_mutex on the fill path")
forbid_text("${fill_path}" "OnQuestRescued" "OnQuestRescued on the fill path (rescue limiter)")
forbid_text("${fill_path}" "sBattleGroundMgr.GetBattleGroundBracketIdFromLevel" "the bot module's bracket formula on the fill path")
require_text("${fill_path}" "ai::bgfill::CheckIntervalMs(sPlayerbotAIConfig.bgCheckIntervalSeconds)" "own timer from CheckIntervalSeconds")
require_order("${fill_path}" "UpdateBgFillMembers();" "ai::bgfill::IntervalDue(" "per-update member pass, then the interval gate")
require_order("${fill_path}" "ai::bgfill::IntervalDue(" "ScanBgFill(nowMs);" "scan only once per interval")
# The timer moves on: without it IntervalDue stays true (last = 0) and the scan runs on every manager update.
require_order("${fill_path}" "ai::bgfill::IntervalDue(" "lastBgFillCheckMs = nowMs ? nowMs : 1;" "timer stamped after the interval gate")
require_order("${fill_path}" "lastBgFillCheckMs = nowMs ? nowMs : 1;" "ScanBgFill(nowMs);" "timer stamped before the scan")

# 4. Scan: real players only, WSG/AB/AV only, core bracket, early return before any bot is touched.
region("${mgr_cpp}" "void RandomPlayerbotMgr::ScanBgFill(uint32 nowMs)" "bool RandomPlayerbotMgr::IsBgFillCandidate(Player* bot) const" scan)
require_text("${scan}" "for (auto const& [playerGuid, player] : players)" "real-player scan over the manager's players list")
require_text("${scan}" "if (!playerAi->IsRealPlayer())\n                continue;" "bots in the players list skipped")
# Final review F2: a queued real player in a loading screen or far teleport still counts (queue state needs no map);
# otherwise his waiting fill bots would leave as abandoned.
region("${scan}" "for (auto const& [playerGuid, player] : players)" "realPlayerQueued = true;" real_count)
require_text("${real_count}" "if (!player || !player->InBattleGroundQueue())\n            continue;" "real player counted while queued")
forbid_text("${real_count}" "IsInWorld" "real player out of the world dropped from the count")
forbid_text("${real_count}" "GetMap()" "real player count tied to his map")
require_text("${scan}" "if (!ai::bgfill::IsFillQueueType(uint32(queueTypeId)))" "only fill queue types")
require_text("${scan}" "if (player->IsInvitedForBattleGroundQueueType(queueTypeId))\n                continue;" "invited players skipped")
require_text("${scan}" "if (player->InBattleGround() && player->GetBattleGround() && player->GetBattleGround()->GetTypeId() == bgTypeId)\n                continue;" "players already in a battleground of that type skipped")
# Owner 16.10. (OB-00): a real player queued with a group (his own bots too) has a solo player's priority. His side's
# real count is his whole core queue entry (GroupQueueInfo through the core's locked reader GetPlayerGroupInfoData),
# every member, once per entry (a second real player of the entry is among the members counted), with the entry's
# own bracket (the core queue's, BattleGroundHandler: the leader's Player::GetBattleGroundBracketIdFromLevel) and team.
require_text("${scan}" "std::set<std::pair<uint32, uint32>> realEntryMembers;" "scan-local set of the counted entries' members")
set(entry_dedup "if (realEntryMembers.count(std::make_pair(uint32(queueTypeId), player->GetGUIDLow())))\n                continue;")
set(entry_read "if (!sBattleGroundMgr.m_BattleGroundQueues[queueTypeId].GetPlayerGroupInfoData(player->GetObjectGuid(), &entryInfo))\n                continue;")
set(entry_members "for (auto const& member : entryInfo.Players)\n                realEntryMembers.insert(std::make_pair(uint32(queueTypeId), member.first.GetCounter()));")
set(entry_size "uint32 const entrySize = uint32(entryInfo.Players.size());")
set(entry_count "real[queueTypeId][bracketId][team] += entrySize;")
set(groupsize_skip "if (entrySize > bgTemplate->GetMaxPlayersPerTeam())\n            {\n                if (trace)\n                    sLog.outBasic(\"[BGFill] state=skip reason=groupsize bg=%u bracket=%u side=%s size=%u max=%u\", uint32(bgTypeId),\n                        uint32(bracketId), team ? \"H\" : \"A\", entrySize, bgTemplate->GetMaxPlayersPerTeam());\n                continue;\n            }")
foreach(piece "${entry_dedup}" "${entry_read}" "${entry_members}" "${entry_size}" "${entry_count}" "${groupsize_skip}"
    "if (entryInfo.IsInvitedToBGInstanceGUID || entryInfo.BgTypeId != bgTypeId)\n                continue;"
    "BattleGroundBracketId const bracketId = entryInfo.BracketId;"
    "if (bracketId == BG_BRACKET_ID_NONE || bracketId >= MAX_BATTLEGROUND_BRACKETS)\n                continue;"
    "uint32 const team = entryInfo.GroupTeam == ALLIANCE ? 0 : 1;")
  require_text("${real_count}" "${piece}" "real side counted per queue entry")
endforeach()
require_order("${real_count}" "${entry_dedup}" "${entry_read}" "an entry already counted is not read again")
require_order("${real_count}" "${entry_read}" "${entry_members}" "members taken from the core entry")
# The members are remembered also for an entry too large to fill: its other real players are not counted either.
require_order("${real_count}" "${entry_members}" "${groupsize_skip}" "members remembered before the group size skip")
require_order("${real_count}" "${groupsize_skip}" "${entry_count}" "a group larger than a side is not counted")
require_order("${scan}" "${entry_count}" "realPlayerQueued = true;" "the counted entry makes a real player wait")
count_text("${real_count}" "GetPlayerGroupInfoData(" 1 "one entry read per real player and queue slot")
forbid_text("${scan}" "++real[" "real side counted per real player instead of per queue entry")
forbid_text("${real_count}" "player->GetTeam()" "real side from the player instead of the entry")
# No new locks on core structures and no unlocked reads of the core queue's maps: only the core's own reader.
foreach(lock "lock_guard" "unique_lock" "m_Lock." "m_QueuedPlayers" "m_QueuedGroups")
  forbid_text("${fill_path}" "${lock}" "lock or raw read of the core queue on the fill path")
endforeach()
require_text("${scan}" "bot->GetBattleGroundBracketIdFromLevel(r.bgTypeId) != r.bracketId" "core bracket formula for the bot")
set(early_return "if (!realPlayerQueued)\n        return;")
require_text("${scan}" "${early_return}" "early return without a real player")
foreach(later "CountBgFillRunning()" "GetAllBots()" "IsBgFillCandidate(bot)" "BGJoinAction::SendBattlemasterJoin(" "ScheduleQueueUpdate(")
  require_order("${scan}" "${early_return}" "${later}" "early return before ${later}")
endforeach()
require_text("${scan}" "bgTemplate->GetMinPlayersPerTeam()" "template minimum per team")
require_text("${scan}" "uint32 const minPerTeam = bgTemplate->GetMinPlayersPerTeam();" "need uses the template minimum")
# Final review F1: Alterac.MinPlayersInQueue above the template minimum makes the core shuffle more entries than it
# takes (BattleGroundQueue::Update), so the real player could be left out; the fill stays out of that AV bracket.
require_text("${scan}" "return bgTypeId == BATTLEGROUND_AV &&\n            ai::bgfill::QueueMinBlocksFill(bgTemplate->GetMinPlayersPerTeam(), sWorld.getConfig(CONFIG_UINT32_AV_MIN_PLAYERS_IN_QUEUE));" "AV blocked by a queue minimum above the template minimum")
set(av_skip "if (avQueueMinBlocks(bgTypeId, bgTemplate))\n            {\n                if (trace)\n                    sLog.outBasic(\"[BGFill] state=skip reason=avqueuemin bg=%u bracket=%u\", uint32(bgTypeId), b);\n                continue;\n            }")
require_text("${scan}" "${av_skip}" "AV skip with its trace line")
foreach(later "uint32 const minPerTeam = bgTemplate->GetMinPlayersPerTeam();" "runningBgs = CountBgFillRunning();" "requests.push_back(")
  require_order("${scan}" "${av_skip}" "${later}" "AV skip before ${later}")
endforeach()
foreach(old "fillMin(" "FillMinPerTeam")
  forbid_text("${scan}" "${old}" "fill minimum raised to Alterac.MinPlayersInQueue")
  forbid_text("${policy_h}" "${old}" "fill minimum raised to Alterac.MinPlayersInQueue")
endforeach()
region("${policy_h}" "inline bool QueueMinBlocksFill(" "inline bool CapAllows(" queue_min)
require_text("${queue_min}" "return queueMin > templateMin;" "blocked only when the queue minimum is above the template minimum")
require_text("${scan}" "ai::bgfill::NeedPerSide(minPerTeam, real[q][b][0], pending[q][b][0])" "need alliance")
require_text("${scan}" "ai::bgfill::NeedPerSide(minPerTeam, real[q][b][1], pending[q][b][1])" "need horde")
require_text("${scan}" "if (!ai::bgfill::CapAllows(running, sPlayerbotAIConfig.bgMaxConcurrent))" "fail-closed cap")
require_text("${scan}" "runningBgs = CountBgFillRunning();" "running battlegrounds counted")
require_text("${scan}" "uint32 const running = ai::bgfill::RunningForBracket(runningBgs, formingBrackets, forming[q][b]);" "cap fed with running and forming")
require_order("${scan}" "if (!ai::bgfill::CapAllows(running, sPlayerbotAIConfig.bgMaxConcurrent))" "requests.push_back(" "cap before any request")
require_text("${scan}" "sBattleGroundMgr.ScheduleQueueUpdate(queueTypeId, bgTypeId, bracketId);" "R11 queue kick")
require_text("${scan}" "bots.upper_bound(bgFillRotate)" "rotating start of the candidate loop")
count_text("${scan}" "collected < wanted" 2 "both halves of the rotating candidate loop stop once enough are collected")
require_text("${scan}" "ai::bgfill::CandidateLimit(r.need)" "candidates bounded per side")
require_text("${scan}" "ai->SetBgFillPending(true);" "pending flag set by the manager")
require_order("${scan}" "BGJoinAction::SendBattlemasterJoin(ai, bot, r.bgTypeId, false);" "ai->SetBgFillPending(true);" "flag only after a join the core took")
# Pending bots of a bracket nobody waits for any more, past PendingMaxSeconds, or beyond the side's room leave their
# queue (they would hold FIFO places and the cap), before the forming brackets are counted; invited ones stay.
foreach(reason abandoned expired surplus)
  require_text("${scan}" "reason = \"${reason}\";" "pending bots leave their queue: ${reason}")
endforeach()
require_text("${scan}" "if (!real[entry.queueType][entry.bracket][0] && !real[entry.queueType][entry.bracket][1])\n            reason = \"abandoned\";" "abandoned = no real player waits in that bracket")
require_text("${scan}" "ai::bgfill::PendingExpired(nowMs, entry.joinMs, ai::bgfill::PendingMaxSeconds * IN_MILLISECONDS)" "pending age bound")
require_text("${scan}" "uint32 const room = bgTemplate && !avQueueMinBlocks(bgTypeId, bgTemplate) ?\n                ai::bgfill::NeedPerSide(bgTemplate->GetMinPlayersPerTeam(), real[entry.queueType][entry.bracket][entry.team], 0) : 0;" "surplus room = min - real of the side, none in a blocked AV bracket")
require_text("${scan}" "if (!ai || bot->IsInvitedForBattleGroundQueueType(BattleGroundQueueTypeId(entry.queueType)))\n        {\n            ++it;\n            continue;\n        }" "invited pending bots are left to the accept")
# Final review F6: a leave the core refused keeps the entry (no erase, no leave counted).
set(scan_leave "if (!LeaveBgFillQueue(bot, ai, botGuid, entry, reason))\n        {\n            ++it;\n            continue;\n        }")
require_text("${scan}" "${scan_leave}" "pending cleanup keeps a bot the core did not let out")
require_order("${scan}" "${scan_leave}" "for (auto const& [botGuid, entry] : bgFillPending)" "pending cleanup before the forming count")
require_order("${scan}" "${scan_leave}" "${early_return}" "pending cleanup also without a real player")
# Final review F7: a bot that left in this scan is not picked again by its candidate loop (local set, no AI value).
require_order("${scan}" "${scan_leave}" "leftThisScan.insert(botGuid);\n        it = bgFillPending.erase(it);" "only a bot that left is remembered, then erased")
require_text("${scan}" "std::set<uint32> leftThisScan;" "scan-local set of the bots that left")
require_order("${scan}" "if (leftThisScan.count(guid))\n            return;" "if (!IsBgFillCandidate(bot))" "bots that left skipped before the candidate check")
region("${mgr_cpp}" "bool RandomPlayerbotMgr::LeaveBgFillQueue(" "uint32 RandomPlayerbotMgr::CountBgFillRunning() const" leave)
require_order("${leave}" "BGStatusAction::LeaveQueue(bot, BattleGroundTypeId(entry.bgTypeId));" "ai->SetBgFillPending(false);" "leave the core queue, then clear the flag")
set(leave_verify "if (bot->InBattleGroundQueueForBattleGroundQueueType(BattleGroundQueueTypeId(entry.queueType)))")
require_order("${leave}" "BGStatusAction::LeaveQueue(bot, BattleGroundTypeId(entry.bgTypeId));" "${leave_verify}" "queue checked again after the leave")
require_order("${leave}" "${leave_verify}" "sLog.outBasic(\"[BGFill] state=leave_failed bot=%u bg=%u reason=%s\"" "refused leave logged")
require_order("${leave}" "state=leave_failed" "return false;" "refused leave reported to the caller")
require_order("${leave}" "return false;" "ai->SetBgFillPending(false);" "flag kept on a refused leave")
require_order("${leave}" "return false;" "++bgFillMinute.leaves;" "refused leave not counted")
require_order("${leave}" "++bgFillMinute.leaves;" "return true;" "leave counted, then reported")
# Every caller looks at the result.
region("${mgr_cpp}" "void RandomPlayerbotMgr::CheckBgFill()" "bool RandomPlayerbotMgr::LeaveBgFillQueue(" leave_callers)
count_text("${leave_callers}" "LeaveBgFillQueue(bot, ai, " 2 "two LeaveBgFillQueue callers")
count_text("${leave_callers}" "if (LeaveBgFillQueue(bot, ai, " 1 "member pass checks the leave result")
count_text("${leave_callers}" "if (!LeaveBgFillQueue(bot, ai, " 1 "scan checks the leave result")
region("${join_cpp}" "void BGStatusAction::LeaveQueue(" "void BGStatusAction::SendPort(" leave_queue)
require_text("${leave_queue}" "SendPort(bot, bgTypeId, 0x0);" "leave = port action 0")
require_text("${policy_h}" "return now - joinMs >= maxMs;" "pending age with an unsigned difference")
# Recheck c6 (open point 3): AiPlayerbot.BgBotTeamCap 0 takes a battleground away from bots in the stage-a fill too - the
# skip compares with 0 exactly, continues to the next bracket, and comes before the cap count, the forming mark and any
# request (so no bot is queued for that battleground).
set(scan_teamcap "            if (sPlayerbotAIConfig.GetBgBotTeamCap(uint32(bgTypeId)) == 0)\n            {\n                if (trace)\n                    sLog.outBasic(\"[BGFill] state=skip reason=teamcap bg=%u bracket=%u side=%s\", uint32(bgTypeId), b,\n                        needA && needH ? \"AH\" : needA ? \"A\" : \"H\");\n                continue;\n            }\n")
require_text("${scan}" "${scan_teamcap}" "BgBotTeamCap 0 skips the bracket in the fill scan")
count_text("${scan}" "GetBgBotTeamCap(" 1 "one team-cap check in the fill scan")
require_order("${scan}" "${scan_teamcap}" "uint32 const running = ai::bgfill::RunningForBracket(" "the team cap before the cap count")
require_order("${scan}" "${scan_teamcap}" "                forming[q][b] = true;" "the team cap before the bracket is marked forming")
require_order("${scan}" "${scan_teamcap}" "requests.push_back(" "the team cap before any request")
# Review c8 (probe r8b): a slot reserved for a waiting real player (stage b) skips that one bracket - continue, not break
# (which would end the bracket loop of the type) - before the team cap, the cap count, the forming mark and any request.
set(scan_reserved "            if ((needA && BgFillReserved(q, b, 0, nowMs)) || (needH && BgFillReserved(q, b, 1, nowMs)))\n            {\n                if (trace)\n                    sLog.outBasic(\"[BGFill] state=skip reason=reserved bg=%u bracket=%u side=%s\", uint32(bgTypeId), b,\n                        needA && needH ? \"AH\" : needA ? \"A\" : \"H\");\n                continue;\n            }\n")
require_text("${scan}" "${scan_reserved}" "a reserved slot skips the bracket in the fill scan")
count_text("${scan}" "BgFillReserved(" 2 "one reservation check (both sides) in the fill scan")
require_order("${scan}" "${scan_reserved}" "${scan_teamcap}" "the reservation before the team cap")
require_order("${scan}" "${scan_reserved}" "uint32 const running = ai::bgfill::RunningForBracket(" "the reservation before the cap count")
require_order("${scan}" "${scan_reserved}" "                forming[q][b] = true;" "the reservation before the bracket is marked forming")

# 5. Candidate filters.
# Ends where the pending recheck starts, so a filter dropped here is not found there.
region("${mgr_cpp}" "bool RandomPlayerbotMgr::IsBgFillCandidate(Player* bot) const" "bool RandomPlayerbotMgr::IsBgFillStillFree(" candidate)
foreach(filter
    "IsPersistentRosterMember(guid)"
    "!bot->IsAlive()"
    "IsParkedBot(guid) || ai->IsParked()"
    "if (bot->GetGroup())\n        return false;"
    "bot->InBattleGround() || !bot->GetMap() || bot->GetMap()->Instanceable()"
    "bot->IsTaxiFlying() || bot->GetTransport() || bot->IsInCombat() || bot->IsBeingTeleportedFar()"
    "if (ai->IsQuestRescueBusy())\n        return false;"
    "if (ai->HasRealPlayerMaster())\n        return false;"
    "if (!bot->CanJoinToBattleground())\n        return false;"
    "ai::bgfill::MinLoggedInSeconds * IN_MILLISECONDS"
    "if (!bot->HasFreeBattleGroundQueueId())\n        return false;"
    "if (OnForcedTravel(ai))\n        return false;")
  require_text("${candidate}" "${filter}" "candidate filter")
endforeach()
forbid_text("${candidate}" "IsTank(" "tank/healer filter for roster bots")
# Owner 16.10. (OB-00): the bots of a real player's group are never free roster bots (the group filter says so).
require_text("${candidate}" "player's own bots, and every bot of a real player's group, are never free roster bots" "owner rule at the group filter")
require_text("${conf_dist}" "the group stays together and counts as a whole toward its side's minimum" "PlayerFill documents the group rule")
require_text("${conf_dist}" "a real player's group are never taken as free bots" "PlayerFill documents his bots are never free")
# OB-10 review: a forced trip (guild founding, meeting, errand) keeps the bot out, also while still in PREPARE.
region("${mgr_cpp}" "static bool OnForcedTravel(PlayerbotAI* ai)" "bool RandomPlayerbotMgr::IsBgFillCandidate(Player* bot) const" forced_travel)
require_text("${forced_travel}" "if (!target || !target->IsForced())\n        return false;" "only forced trips")
require_text("${forced_travel}" "return status != TravelStatus::TRAVEL_STATUS_NONE && status != TravelStatus::TRAVEL_STATUS_EXPIRED;" "every live trip status, PREPARE included")
forbid_text("${forced_travel}" "IsActive()" "IsActive() leaves PREPARE out")
# The running count: ended battlegrounds out; started-for-fill instances counted from the accept on (before the bots
# show up in GetPlayers()); a running instance the core only topped up with fill bots counts only with another roster
# bot inside (final review F5: skipping it whole undercounted).
region("${mgr_cpp}" "uint32 RandomPlayerbotMgr::CountBgFillRunning() const" "void RandomPlayerbotMgr::ReportBgFillMinute()" running_count)
require_text("${running_count}" "bg->GetStatus() == STATUS_WAIT_LEAVE)\n            return;" "ended battlegrounds not counted")
# Stage c: an instance a bot-own match started (startedForOwn) counts against the cap exactly like a started-for-fill one.
require_text("${running_count}" "(entry.startedForFill || entry.startedForOwn ? started : toppedUp).emplace(entry.bgTypeId, entry.instanceId);" "fill and own instances from the in-battleground set")
require_order("${running_count}" "if (started.count(key))\n        {\n            ++running;\n            return;\n        }" "bool const toppedUpByFill = toppedUp.count(key) != 0;" "started instances counted, then the topped-up check")
forbid_text("${running_count}" "if (toppedUp.count(key))\n            return;" "topped-up instance skipped whole")
require_text("${running_count}" "uint32 const playerLow = playerGuid.GetCounter();\n            if (!IsPersistentRosterMember(playerLow))\n                continue;" "other battlegrounds with a roster bot counted")
require_text("${running_count}" "if (toppedUpByFill && bgFillInBg.count(playerLow))\n                continue;" "only the topped-up fill bots themselves are left out")
require_order("${running_count}" "if (toppedUpByFill && bgFillInBg.count(playerLow))\n                continue;\n            ++running;\n            return;" "return running;" "a topped-up instance with another roster bot counts")
require_text("${conf_dist}" "topped up with fill bots is not counted unless it holds other roster bots too" "MaxConcurrent documents what the cap counts")
count_text("${running_count}" "++running;" 2 "running counted per battleground")
count_text("${running_count}" "return running;" 1 "one exit of the running count")
require_order("${running_count}" "sBattleGroundMgr.ApplyAllBattleGrounds(" "return running;" "running counted before it is returned")
forbid_text("${running_count}" "return 0" "running count short-circuited")
forbid_text("${candidate}" "IsHeal(" "tank/healer filter for roster bots")
require_text("${ai_h}" "bool IsQuestRescueBusy() const { return questRescueRequested.load() || questRescueDone.load(); }" "quest rescue busy = requested or done")

# 6. Accept on the world thread; BGStatusAction does not accept for a fill bot.
region("${mgr_cpp}" "void RandomPlayerbotMgr::UpdateBgFillMembers()" "void RandomPlayerbotMgr::ScanBgFill(uint32 nowMs)" members)
require_text("${members}" "BGStatusAction::AcceptInvite(ai, bot, BattleGroundTypeId(entry.bgTypeId))" "manager accepts the invite")
require_order("${members}" "bot->IsInvitedForBattleGroundQueueType(queueTypeId)" "BGStatusAction::AcceptInvite(" "accept only once invited")
require_text("${members}" "!bot->InBattleGroundQueueForBattleGroundQueueType(queueTypeId)" "stale pending entries dropped")
# OB-10 checked again before the accept: a bot not invited yet that got parked, grouped, went into a dungeon, a rescue
# or a forced trip meanwhile leaves the queue. Final review F3: an invited bot leaves only for hard reasons (a decline
# leaves a hole in the real player's match); a quest rescue only delays its accept.
set(busy_check "if (!invited && !IsBgFillStillFree(bot, ai))\n            leaveReason = \"busy\";")
set(unfit_check "else if (invited && !IsBgFillInviteStillFit(bot, ai))\n            leaveReason = \"unfit\";")
require_text("${members}" "bool const invited = bot->IsInvitedForBattleGroundQueueType(queueTypeId);" "invite state read once")
require_text("${members}" "${busy_check}\n        ${unfit_check}" "full recheck before the invite, hard reasons after it")
# Owner 16.10.: a bot in a group's core queue entry (more than one member) belongs to that group, never to the fill:
# its pending entry is dropped without a leave, before the busy/unfit leaves could take it out of the group's entry.
set(group_entry_drop "if (bot->GetGroup() && InGroupQueueEntry(bot, queueTypeId))\n        {\n            if (sPlayerbotAIConfig.bgTrace)\n                sLog.outBasic(\"[BGFill] state=drop bot=%u bg=%u reason=groupentry\", guid, entry.bgTypeId);\n            ai->SetBgFillPending(false);\n            it = bgFillPending.erase(it);\n            continue;\n        }")
require_text("${members}" "${group_entry_drop}" "a bot in a group's queue entry is no fill bot")
require_order("${members}" "${group_entry_drop}" "${busy_check}" "group entry drop before the busy and unfit leaves")
region("${mgr_cpp}" "static bool InGroupQueueEntry(Player* bot, BattleGroundQueueTypeId queueTypeId)" "void RandomPlayerbotMgr::CheckBgFill()" group_entry)
require_text("${group_entry}" "return sBattleGroundMgr.m_BattleGroundQueues[queueTypeId].GetPlayerGroupInfoData(bot->GetObjectGuid(), &entryInfo) &&\n        entryInfo.Players.size() > 1;" "group entry = core entry with more than one member")
foreach(lock "lock_guard" "unique_lock" "m_Lock." "m_QueuedPlayers" "LeaveBgFillQueue")
  forbid_text("${group_entry}" "${lock}" "lock, raw core read or leave in the group entry check")
endforeach()
require_order("${members}" "${busy_check}" "BGStatusAction::AcceptInvite(" "still free before the accept")
require_text("${members}" "if (LeaveBgFillQueue(bot, ai, guid, entry, leaveReason))\n                it = bgFillPending.erase(it);\n            else\n                ++it;\n            continue;" "a busy or unfit pending bot leaves its queue, kept when the core refuses")
require_order("${members}" "if (leaveReason)" "if (!invited)\n        {\n            ++it;\n            continue;\n        }" "leave check before the not-invited wait")
require_order("${members}" "if (!invited)\n        {\n            ++it;\n            continue;\n        }" "BGStatusAction::AcceptInvite(" "accept only once invited")
require_order("${members}" "if (ai->IsQuestRescueBusy())\n        {\n            ++it;\n            continue;\n        }" "BGStatusAction::AcceptInvite(" "quest rescue delays the accept")
forbid_text("${members}" "IsBgFillStillFree(bot, ai))\n        {" "full recheck applied to invited bots")
region("${mgr_cpp}" "bool RandomPlayerbotMgr::IsBgFillStillFree(" "bool RandomPlayerbotMgr::IsBgFillInviteStillFit(" still_free)
foreach(check "IsPersistentRosterMember(guid)" "IsParkedBot(guid) || ai->IsParked()" "bot->GetGroup()" "ai->HasRealPlayerMaster()" "ai->IsQuestRescueBusy()" "bot->GetMap()->Instanceable()"
    "if (OnForcedTravel(ai))\n        return false;")
  require_text("${still_free}" "${check}" "pending recheck")
endforeach()
region("${mgr_cpp}" "bool RandomPlayerbotMgr::IsBgFillInviteStillFit(" "bool RandomPlayerbotMgr::LeaveBgFillQueue(" invite_fit)
foreach(check "if (!IsPersistentRosterMember(guid) || IsParkedBot(guid) || ai->IsParked())\n        return false;" "if (ai->HasRealPlayerMaster())\n        return false;"
    "return !bot->InBattleGround() && !bot->GetMap()->Instanceable();")
  require_text("${invite_fit}" "${check}" "invited bot hard reason")
endforeach()
# OB-10 (#420): a group with a real player stays a hard reason after the invite; an ad-hoc bot group does not.
require_text("${invite_fit}" "if (bot->GetGroup() && GroupHasRealPlayer(bot->GetGroup()))\n        return false;" "group with a real player declines")
string(REPLACE "if (bot->GetGroup() && GroupHasRealPlayer(bot->GetGroup()))" "" invite_fit_rest "${invite_fit}")
foreach(transient "GetGroup()" "OnForcedTravel" "IsQuestRescueBusy")
  forbid_text("${invite_fit_rest}" "${transient}" "transient reason declines an invite")
endforeach()
region("${mgr_cpp}" "static bool GroupHasRealPlayer(Group* group)" "bool RandomPlayerbotMgr::IsBgFillCandidate(Player* bot) const" real_group)
require_text("${real_group}" "if (!memberAi || memberAi->IsRealPlayer() || memberAi->HasRealPlayerMaster())\n            return true;" "real player or a real player's bot")
forbid_text("${real_group}" "FindMap()" "every online member counts, on any map")
# The flag stays set from the accept until the return: a WAIT_JOIN latched before the accept must not port again.
region("${members}" "BGStatusAction::AcceptInvite(" "bgFillInBg[guid] = accepted;" accept_tail)
forbid_text("${accept_tail}" "SetBgFillPending(false)" "pending flag cleared at the accept")
# Stage c: a bot-own match's bot (entry.own) never marks an instance started for the fill (startedForOwn instead).
require_text("${accept_tail}" "accepted.startedForFill = !entry.pairFor && !entry.own && bg && bg->GetStatus() != STATUS_IN_PROGRESS;" "instance started for the fill (never by a stage-b pair or top-up bot or a stage-c own bot), or topped up")
require_order("${members}" "ai->OnReturnFromBattleground(uint32(time(nullptr)));" "ai->SetBgFillPending(false);\n        ++bgFillMinute.returns;" "pending flag cleared on the return")
require_text("${ai_h}" "std::atomic<bool> bgFillPending{ false };" "pending flag is atomic")
region("${join_cpp}" "if (statusid == STATUS_WAIT_JOIN)" "bool BGStatusCheckAction::Execute(Event& event)" wait_join)
require_order("${wait_join}" "if (ai->IsBgFillPending())\n            return true;" "ai->Unmount();" "fill bots skip the self-accept")
require_order("${wait_join}" "if (ai->IsBgFillPending())\n            return true;" "HandleBattleFieldPortOpcode(packet);" "fill bots skip the port")
require_text("${wait_join}" "ApplyPortState(ai);" "WAIT_JOIN side effects through the shared helper")
region("${join_cpp}" "bool BGStatusAction::AcceptInvite(" "bool BGStatusCheckAction::Execute(Event& event)" accept)
require_order("${accept}" "SendPort(bot, bgTypeId, 0x1);" "ApplyPortState(ai);" "port action 1, then the side effects")
region("${join_cpp}" "void BGStatusAction::SendPort(" "bool BGStatusCheckAction::Execute(Event& event)" send_port)
require_text("${send_port}" "bot->GetSession()->HandleBattleFieldPortOpcode(packet);" "core port handler")
require_text("${join_h}" "static void ApplyPortState(PlayerbotAI* ai);" "shared port side effects")
region("${join_cpp}" "void BGStatusAction::ApplyPortState(PlayerbotAI* ai)" "bool BGStatusAction::AcceptInvite(" port_state)
require_text("${port_state}" "ai->ResetStrategies(false);" "strategies reset at port")
require_text("${port_state}" "ai->ChangeStrategy(\"+pvp\", BotState::BOT_STATE_NON_COMBAT);" "+pvp at port")
require_text("${port_state}" "context->GetValue<uint32>(\"bg role\")->Set(urand(0, 9));" "bg role at port")

# 7. Join: the packet and bypass of JoinQueue, shared, not duplicated.
require_text("${join_h}" "static void SendBattlemasterJoin(PlayerbotAI* ai, Player* bot, BattleGroundTypeId bgTypeId, bool joinAsGroup, uint32 instanceId = 0);" "shared join helper (stage b: the pair bots' instance, 0 = first available by default)")
region("${join_cpp}" "bool BGJoinAction::JoinQueue(uint32 type)" "void BGJoinAction::SendBattlemasterJoin(" join_queue)
require_text("${join_queue}" "SendBattlemasterJoin(ai, bot, bgTypeId, joinAsGroup);" "JoinQueue uses the shared helper")
require_text("${join_queue}" "sRandomPlayerbotMgr.Refresh(bot);" "legacy random bots keep their Refresh")
region("${join_cpp}" "void BGJoinAction::SendBattlemasterJoin(" "bool FreeBGJoinAction::shouldJoinBg(" join_helper)
require_text("${join_helper}" "ai->GetAiObjectContext()->GetValue<uint32>(\"bg type\")->Set(0);" "bg type reset as JoinQueue did")
require_text("${join_helper}" "ObjectGuid guid = ObjectGuid(uint64(1337));" "battlemaster bypass guid")
require_text("${join_helper}" "bot->GetSession()->HandleBattlemasterJoinOpcode(packet);" "core join handler")
count_text("${join_cpp}" "HandleBattlemasterJoinOpcode(packet);" 1 "one battlemaster join packet in the file")
# No Refresh for fill bots also through the shared helper; the legacy JoinQueue keeps its one call.
forbid_text("${join_helper}" "Refresh(" "Refresh in the join helper the fill calls")
count_text("${join_cpp}" "sRandomPlayerbotMgr.Refresh(bot);" 1 "Refresh only in the legacy JoinQueue")

# 8. [BGFill] lines only behind AiPlayerbot.BG.Trace.
require_text("${fill_path}" "bool const trace = sPlayerbotAIConfig.bgTrace;" "trace switch read once per scan")
require_guarded("${fill_path}" "sLog.outBasic(\"[BGFill]" "if (trace" "if (sPlayerbotAIConfig.bgTrace)" 80 "[BGFill] line without the trace switch")
# An inverted or widened guard would log with Trace off.
foreach(inverted "!trace" "!sPlayerbotAIConfig.bgTrace" "trace ||" "bgTrace ||")
  forbid_text("${fill_path}" "${inverted}" "inverted or widened trace guard")
endforeach()
foreach(state queue join accept cap skip return leave leave_failed drop)
  require_text("${fill_path}" "\"[BGFill] state=${state} " "[BGFill] state=${state} line")
endforeach()
require_text("${fill_path}" "\"[BGFill] minute checks=%u joins=%u accepts=%u returns=%u pending=%u in_bg=%u scan_us_max=%u leaves=%u\"" "minute summary")
forbid_text("${join_cpp}" "[BGFill]" "[BGFill] lines outside the manager")

# 9. Return: OnReturnFromBattleground does exactly the questRescueDone reset, never OnQuestRescued.
require_text("${ai_h}" "void OnReturnFromBattleground(uint32 now);" "return hook declared")
region("${ai_cpp}" "void PlayerbotAI::OnReturnFromBattleground(uint32 now)" "void PlayerbotAI::UpdateAI(" return_hook)
require_text("${return_hook}" "aiObjectContext->GetValue<int>(\"manual int\", \"quest search stage\")->Set(0);" "quest search stage 0")
require_text("${return_hook}" "questProgress.lastChange = now;" "idle watch reset")
forbid_text("${return_hook}" "OnQuestRescued" "OnQuestRescued in the return hook (rescue limiter)")
forbid_text("${return_hook}" "lastQuestRescue" "lastQuestRescue in the return hook (rescue limiter)")
count_text("${return_hook}" ";" 2 "exactly two statements in the return hook")
require_order("${members}" "ai->OnReturnFromBattleground(uint32(time(nullptr)));" "target->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);" "return hook, then travel status NONE")
forbid_text("${members}" "SetTarget(" "travel target restored 1:1")

# 10. BR (OB-00 D4) and SV (stage d) are not filled.
region("${policy_h}" "inline bool IsFillQueueType(uint32_t queueType)" "inline uint32_t NeedPerSide(" fill_types)
require_text("${fill_types}" "return queueType == QueueAV || queueType == QueueWS || queueType == QueueAB;" "WSG/AB/AV only")
forbid_text("${fill_types}" "QueueBR" "Blood Ring filled")
forbid_text("${fill_types}" "QueueSV" "Sunnyglade Valley filled")
require_text("${policy_h}" "return running < maxConcurrent;" "cap fails closed at 0")
forbid_text("${policy_h}" "mutex" "locks in the policy")
forbid_text("${policy_h}" "static " "static state in the policy")

# 11. World-thread ownership of the fill sets.
require_text("${mgr_h}" "std::map<uint32, BgFillEntry> bgFillPending;" "pending set")
require_text("${mgr_h}" "std::map<uint32, BgFillEntry> bgFillInBg;" "in-battleground set")
forbid_text("${join_cpp}" "bgFillPending[" "pending set written outside the manager")

message(STATUS "bg_fill_517 source contract passed")
