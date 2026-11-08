# twow-repo#563 X5 (CLI-ROT 02.11.2026, OB-00): AiPlayerbot.ThreadSafeTeleportAck (default 1 since 06.11.2026). A master's bot holder
# (PlayerbotMgr, run inside the master's Map::Update) no longer acks the near teleport of a bot in world - that bot may be on
# another map, updated by another thread (crash 02.11.2026: MotionMaster::DirectClean from HandleTeleportAck -> StopMoving). The
# bot's own UpdateAI acks it. Pins: switch default 1 (OB-00 06.11.2026, #676 crash B), only 0 = off, in [ConfigEcho]; pure policy; the holder switch with the old
# ack as the Holder case; only PlayerbotMgr is a master holder; the bot-thread ack at the start of UpdateAI (before the park
# check), guarded by the request flag and the near-teleport check; the [WorldBots] suffix only when on.
# twow-repo#676 (CLI-ROT 05.11.2026, BOT-HOLDER-FARACK-01, no switch): no HandleTeleportAck / HandleMoveWorldportAckOpcode of a
# master holder's bot from inside a map update. A far teleport of a master holder's bot is only queued by guid (FarAcks) in
# UpdateSessions; PlayerbotHolder::DrainFarTeleportAcks acks it from PlayerbotWorldScript::OnUpdate (after sMapMgr.Update) and
# nowhere else; the holder's own ack is reached only for what the policy leaves to it (never a far teleport of a master holder).
# Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/TeleportAckThreadPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/PlayerbotMgr.cpp" mgr_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotMgr.h" mgr_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" rnd_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotScripts.cpp" scripts_cpp)

# text from `start` up to (not including) `stop`, empty when either is missing
function(between text start stop out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${stop}" b)
  if(b EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

# append to `errors` of the calling function instead of failing (functions, not macros: macro arguments would
# re-evaluate the backslashes of the source text)
function(need text needle what)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(errors "${errors};missing ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(ban text needle what)
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    set(errors "${errors};forbidden ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(before text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(errors "${errors};order ${what}: ${first} before ${second}" PARENT_SCOPE)
  endif()
endfunction()
# exactly `n` matches of the regex `re` in `text`
function(count_is text re n what)
  string(REGEX MATCHALL "${re}" hits "${text}")
  list(LENGTH hits got)
  if(NOT got EQUAL n)
    set(errors "${errors};count ${what}: ${re} expected ${n}, found ${got}" PARENT_SCOPE)
  endif()
endfunction()

function(check policy mgr_cpp mgr_h ai_cpp ai_h config_h config_cpp dist rnd_cpp scripts_cpp out_var)
  set(errors "")

  # 1. Switch: default 1 in the dist, the member and the read (OB-00 06.11.2026), only 0 = off (rollback), [ConfigEcho] in sorted place.
  need("${dist}" "\nAiPlayerbot.ThreadSafeTeleportAck = 1\n" "dist key, default 1")
  need("${dist}" "BEHAVIOUR CHANGE, ON by default, 0 = rollback" "dist text, owner default-1 rule (#740)")
  need("${config_h}" "bool threadSafeTeleportAck = true;" "config member, default on")
  need("${config_cpp}" "threadSafeTeleportAck = config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 1) != 0;" "config read, default 1, only 0 = off")
  need("${config_cpp}" "            {\"Supervisor.Arbiter\", Value(supervisorArbiter)},\n            {\"ThreadSafeTeleportAck\", Value(threadSafeTeleportAck)},\n            {\"ThrownUsageFix\"" "[ConfigEcho], sorted (owner rule #615, #710)")
  # #676 has no switch of its own (OB-00 05.11.2026: default on, a switch only for an A/B).
  ban("${dist}" "FarTeleportAck" "a #676 switch in the dist")
  ban("${config_h}" "farTeleportAck" "a #676 switch in the config")

  # 2. Pure policy and its rules.
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc ${incs})
    if(NOT inc MATCHES "^#include <(algorithm|atomic|cstddef|cstdint|mutex|vector)>$")
      list(APPEND errors "TeleportAckThreadPolicy.h is not pure: ${inc}")
    endif()
  endforeach()
  ban("${policy}" "sLog" "logging in the policy")
  need("${policy}" "    if (!masterHolder)\n        return Acker::Holder;\n    if (farTeleport)\n        return Acker::WorldThread;\n    if (on && inWorld && nearTeleport)\n        return Acker::BotThread;\n    return Acker::Holder;" "random holder = holder, master far = world thread (any switch), X5 near in world = bot thread")
  need("${policy}" "    return requested && inWorld && nearTeleport;" "the bot thread acks only a request, in world, near pending")
  need("${policy}" "    return foundWithAi && !inWorld && farTeleport;" "the drain acks only a found bot, out of world, far pending")
  need("${policy}" "        if (std::find(guids.begin(), guids.end(), guidLow) != guids.end())\n            return Queued::Duplicate;\n        if (guids.size() >= maxDepth)\n            return Queued::Full;" "the queue: once per guid, bounded")
  need("${policy}" "        std::lock_guard<std::mutex> lock(mutex);\n        out.swap(guids);" "Take swaps the queue out under the lock")

  # 3. The holder: the old ack is the Holder case; the request and the far queue do not ack; only PlayerbotMgr is a master holder.
  need("${mgr_cpp}" "#include \"playerbot/TeleportAckThreadPolicy.h\"" "policy include (holder)")
  between("${mgr_cpp}" "void PlayerbotHolder::UpdateSessions(uint32 elapsed)" "        else if (bot->IsInWorld())" sessions)
  if(sessions STREQUAL "")
    list(APPEND errors "UpdateSessions ack branch not found")
  endif()
  need("${sessions}" "        ai::teleport_ack::Acker const acker = GetBotAI(bot) && bot->IsBeingTeleported() ?
            ai::teleport_ack::Choose(sPlayerbotAIConfig.threadSafeTeleportAck, IsMasterHolder(), bot->IsInWorld(),
                bot->IsBeingTeleportedNear(), bot->IsBeingTeleportedFar()) : ai::teleport_ack::Acker::Holder;" "the holder asks the policy with the switch, only for a pending teleport")
  need("${sessions}" "        else if (GetBotAI(bot) && bot->IsBeingTeleported())
        {
            GetBotAI(bot)->HandleTeleportAck();
            ++teleportAcks;
        }" "Holder: the old ack and its count, unchanged")
  need("${sessions}" "        if (acker == ai::teleport_ack::Acker::BotThread)
        {
            GetBotAI(bot)->RequestTeleportAck();
" "BotThread: only the request")
  need("${sessions}" "        else if (acker == ai::teleport_ack::Acker::WorldThread)
        {
            ai::teleport_ack::Queued const queued = ai::teleport_ack::FarAcks().Push(bot->GetGUIDLow());" "WorldThread: only the guid is queued")
  before("${sessions}" "Acker::WorldThread)" "        else if (GetBotAI(bot) && bot->IsBeingTeleported())" "the far queue before the old ack")
  count_is("${sessions}" "HandleTeleportAck\\(\\)" 1 "UpdateSessions acks in exactly one place (the Holder case)")
  ban("${sessions}" "HandleMoveWorldportAckOpcode()" "a worldport ack in UpdateSessions")
  ban("${sessions}" "DrainFarTeleportAcks()" "the drain inside UpdateSessions (a map update)")
  need("${mgr_h}" "    virtual bool IsMasterHolder() const { return false; }" "the base holder (random bots) is not a master holder")
  need("${mgr_h}" "    Player* GetMaster() const { return master; };\n    bool IsMasterHolder() const override { return true; }" "PlayerbotMgr is the master holder")

  # 4. The world-thread drain: resolves each guid again, acks only behind WorldThreadAcks, exactly once.
  need("${mgr_h}" "    static void DrainFarTeleportAcks();" "the drain is a static holder function (no holder pointer)")
  between("${mgr_cpp}" "void PlayerbotHolder::DrainFarTeleportAcks()" "void PlayerbotHolder::UpdateSessions(uint32 elapsed)" drain)
  if(drain STREQUAL "")
    list(APPEND errors "DrainFarTeleportAcks not found before UpdateSessions")
  endif()
  need("${drain}" "    if (queue.Empty())\n        return;" "the empty fast path")
  need("${drain}" "    for (uint32 const guidLow : queue.Take())" "the drain takes the whole queue")
  need("${drain}" "ObjectAccessor::FindPlayerNotInWorld(ObjectGuid(HIGHGUID_PLAYER, guidLow))" "each guid resolved again")
  need("${drain}" "        if (ai::teleport_ack::WorldThreadAcks(botAI != nullptr, bot && bot->IsInWorld(), bot && bot->IsBeingTeleportedFar()))\n        {\n            botAI->HandleTeleportAck();" "the ack only behind WorldThreadAcks")
  count_is("${drain}" "HandleTeleportAck\\(\\)" 1 "the drain acks in exactly one place")
  # The far ack runs nowhere else in the module: the two holder sites (Holder case, drain) and the X5 bot-thread site only.
  count_is("${mgr_cpp}" "->HandleTeleportAck\\(\\)" 2 "PlayerbotMgr.cpp ack sites (Holder case + drain)")
  count_is("${ai_cpp}" "\n[ \t]+HandleTeleportAck\\(\\)" 1 "PlayerbotAI.cpp ack sites (the X5 bot-thread ack; no ';' in the regex, it splits the match list)")
  count_is("${scripts_cpp}" "DrainFarTeleportAcks\\(\\)" 1 "one drain call")
  count_is("${mgr_cpp}" "DrainFarTeleportAcks\\(\\)" 1 "PlayerbotMgr.cpp: the definition only, no call")
  ban("${ai_cpp}" "DrainFarTeleportAcks" "the drain from a bot update (map thread)")
  ban("${rnd_cpp}" "DrainFarTeleportAcks" "the drain from the random holder's update")

  # 5. The drain call: PlayerbotWorldScript::OnUpdate (WORLDHOOK_ON_UPDATE, after sMapMgr.Update), before the enabled check.
  between("${scripts_cpp}" "        void OnUpdate(uint32 diff) override" "sRandomPlayerbotMgr.RunQueuedRemoteCommands();" on_update)
  if(on_update STREQUAL "")
    list(APPEND errors "PlayerbotWorldScript::OnUpdate not found")
  endif()
  need("${on_update}" "            PlayerbotHolder::DrainFarTeleportAcks();\n\n            if (!sPlayerbotAIConfig.enabled)\n                return;" "the drain first in OnUpdate, before the enabled check")

  # 6. The bot thread: at the start of UpdateAI (after the inbox drain, before the park check), the flag taken once.
  need("${ai_cpp}" "#include \"playerbot/TeleportAckThreadPolicy.h\"" "policy include (bot)")
  need("${ai_h}" "    void RequestTeleportAck() { teleportAckRequested.store(true, std::memory_order_release); }" "the request sets the flag")
  need("${ai_h}" "    std::atomic<bool> teleportAckRequested{ false };" "atomic request flag, default false")
  between("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)" "    if (parked)" head)
  if(head STREQUAL "")
    list(APPEND errors "UpdateAI head (up to the park check) not found")
  endif()
  need("${head}" "    if (teleportAckRequested.exchange(false, std::memory_order_acq_rel) &&\n        ai::teleport_ack::BotThreadAcks(true, bot->IsInWorld(), bot->IsBeingTeleportedNear()))\n    {\n        HandleTeleportAck();" "the bot-thread ack before the park check, the flag taken once")
  before("${head}" "    HandleQueuedBotPackets();" "    if (teleportAckRequested.exchange(" "the inbox drain stays first")

  # 7. [WorldBots]: the X5 suffix only when on; the #676 counts always, after it; increments.
  need("${rnd_cpp}" "        if (sPlayerbotAIConfig.threadSafeTeleportAck)\n        {\n            ai::teleport_ack::Counters& c = ai::teleport_ack::Global();" "X5 suffix only when on")
  need("${rnd_cpp}" "std::to_string(c.requested.exchange(0))" "requested as increments")
  need("${rnd_cpp}" "            tpAck += \" far_ack_queued=\" + std::to_string(c.farQueued.exchange(0)) +" "#676 counts appended, as increments")
  need("${rnd_cpp}" "\" far_ack_skip=\" + std::to_string(c.farSkips.exchange(0)) + \" far_ack_full=\" + std::to_string(c.farFull.exchange(0));" "#676 skip / full counts")
  need("${rnd_cpp}" "teleport_ack_calls=%u online=%u%s\"," "the format unchanged (#597 pin)")
  need("${rnd_cpp}" "            (hook + tpAck).c_str());" "the suffix after the hook suffix")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${mgr_cpp}" "${mgr_h}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${rnd_cpp}" "${scripts_cpp}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#563 X5 / #676 teleport ack thread contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "teleport ack probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${mgr_cpp}" "${mgr_h}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${rnd_cpp}" "${scripts_cpp}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "teleport ack probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
# X5
probe("default off" dist "\nAiPlayerbot.ThreadSafeTeleportAck = 1\n" "\nAiPlayerbot.ThreadSafeTeleportAck = 0\n")
probe("member off" config_h "bool threadSafeTeleportAck = true;" "bool threadSafeTeleportAck = false;")
probe("read default off" config_cpp "config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 1) != 0;" "config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 0) != 0;")
probe("dist text missing" dist "BEHAVIOUR CHANGE, ON by default, 0 = rollback" "ON by default")
probe("unknown = off" config_cpp "config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 1) != 0;" "config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 1) == 1;")
probe("echo missing" config_cpp "            {\"ThreadSafeTeleportAck\", Value(threadSafeTeleportAck)},\n" "")
probe("off defers near" policy "    if (on && inWorld && nearTeleport)" "    if (inWorld && nearTeleport)")
probe("random holder defers" policy "    if (!masterHolder)\n        return Acker::Holder;\n" "")
probe("near kept on the holder" policy "    if (on && inWorld && nearTeleport)\n        return Acker::BotThread;" "    if (on && inWorld && nearTeleport)\n        return Acker::Holder;")
probe("bot acks without a request" policy "    return requested && inWorld && nearTeleport;" "    return inWorld && nearTeleport;")
probe("impure policy" policy "#include <cstdint>" "#include <cstdint>\n#include \"Entities/Player.h\"")
probe("holder acks the handed one too" mgr_cpp "            GetBotAI(bot)->RequestTeleportAck();
" "            GetBotAI(bot)->RequestTeleportAck();
            GetBotAI(bot)->HandleTeleportAck();
")
probe("switch ignored" mgr_cpp "Choose(sPlayerbotAIConfig.threadSafeTeleportAck, IsMasterHolder()," "Choose(true, IsMasterHolder(),")
probe("every holder a master holder" mgr_h "    virtual bool IsMasterHolder() const { return false; }" "    virtual bool IsMasterHolder() const { return true; }")
probe("bot ack after the park check" ai_cpp "    if (teleportAckRequested.exchange(false, std::memory_order_acq_rel) &&" "    if (false &&")
probe("flag never cleared" ai_cpp "teleportAckRequested.exchange(false, std::memory_order_acq_rel)" "teleportAckRequested.load(std::memory_order_acquire)")
probe("suffix always" rnd_cpp "        if (sPlayerbotAIConfig.threadSafeTeleportAck)\n        {\n            ai::teleport_ack::Counters& c" "        {\n            ai::teleport_ack::Counters& c")
# #676
probe("far kept on the holder" policy "    if (farTeleport)\n        return Acker::WorldThread;" "    if (farTeleport)\n        return Acker::Holder;")
probe("far only with X5" policy "    if (farTeleport)\n        return Acker::WorldThread;" "    if (on && farTeleport)\n        return Acker::WorldThread;")
probe("drain acks in world" policy "    return foundWithAi && !inWorld && farTeleport;" "    return foundWithAi && farTeleport;")
probe("drain acks a done teleport" policy "    return foundWithAi && !inWorld && farTeleport;" "    return foundWithAi && !inWorld;")
probe("queue keeps duplicates" policy "        if (std::find(guids.begin(), guids.end(), guidLow) != guids.end())\n            return Queued::Duplicate;\n" "")
probe("queue unbounded" policy "        if (guids.size() >= maxDepth)\n            return Queued::Full;\n" "")
probe("holder acks the far one too" mgr_cpp "            ai::teleport_ack::Queued const queued = ai::teleport_ack::FarAcks().Push(bot->GetGUIDLow());" "            GetBotAI(bot)->HandleTeleportAck();\n            ai::teleport_ack::Queued const queued = ai::teleport_ack::FarAcks().Push(bot->GetGUIDLow());")
probe("holder drives the worldport" mgr_cpp "            ai::teleport_ack::Queued const queued = ai::teleport_ack::FarAcks().Push(bot->GetGUIDLow());" "            bot->GetSession()->HandleMoveWorldportAckOpcode();\n            ai::teleport_ack::Queued const queued = ai::teleport_ack::FarAcks().Push(bot->GetGUIDLow());")
probe("drain inside the holder pass" mgr_cpp "        else if (acker == ai::teleport_ack::Acker::WorldThread)\n        {\n" "        else if (acker == ai::teleport_ack::Acker::WorldThread)\n        {\n            DrainFarTeleportAcks();\n")
probe("drain without the guard" mgr_cpp "        if (ai::teleport_ack::WorldThreadAcks(botAI != nullptr, bot && bot->IsInWorld(), bot && bot->IsBeingTeleportedFar()))" "        if (botAI)")
probe("drain from the bot update" ai_cpp "    HandleQueuedBotPackets();" "    HandleQueuedBotPackets();\n    PlayerbotHolder::DrainFarTeleportAcks();")
probe("drain never called" scripts_cpp "            PlayerbotHolder::DrainFarTeleportAcks();\n" "")
probe("drain after the enabled check" scripts_cpp "            PlayerbotHolder::DrainFarTeleportAcks();\n\n            if (!sPlayerbotAIConfig.enabled)\n                return;" "            if (!sPlayerbotAIConfig.enabled)\n                return;\n            PlayerbotHolder::DrainFarTeleportAcks();")
probe("far counts missing" rnd_cpp "            tpAck += \" far_ack_queued=\"" "            tpAck = \" far_ack_queued=\"")
probe("a #676 switch" dist "\nAiPlayerbot.ThreadSafeTeleportAck = 1\n" "\nAiPlayerbot.ThreadSafeTeleportAck = 1\nAiPlayerbot.FarTeleportAckWorldThread = 1\n")

message(STATUS "teleport_ack_thread source contract passed (${probes} negative probes)")
