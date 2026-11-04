# twow-repo#563 X5 (CLI-ROT 02.11.2026, OB-00): AiPlayerbot.ThreadSafeTeleportAck (default 0). A master's bot holder
# (PlayerbotMgr, run inside the master's Map::Update) no longer acks the near teleport of a bot in world - that bot may be on
# another map, updated by another thread (crash 02.11.2026: MotionMaster::DirectClean from HandleTeleportAck -> StopMoving). The
# bot's own UpdateAI acks it. Pins: switch default 0, only 1 = on, in [ConfigEcho]; pure policy; the holder switch with the old
# ack as the Holder case; only PlayerbotMgr is a master holder; the bot-thread ack at the start of UpdateAI (before the park
# check), guarded by the request flag and the near-teleport check; the [WorldBots] suffix only when on. Every check runs once on
# the real sources and once per negative probe (a mutated copy that must fail).

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

function(check policy mgr_cpp mgr_h ai_cpp ai_h config_h config_cpp dist rnd_cpp out_var)
  set(errors "")

  # 1. Switch: default 0 in the dist and the member, only 1 = on, [ConfigEcho] in sorted place.
  need("${dist}" "\nAiPlayerbot.ThreadSafeTeleportAck = 0\n" "dist key, default 0")
  need("${config_h}" "bool threadSafeTeleportAck = false;" "config member, default off")
  need("${config_cpp}" "threadSafeTeleportAck = config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 0) == 1;" "config read, only 1 = on")
  need("${config_cpp}" "            {\"Rpg.TurnInFirst\", Value(rpgTurnInFirst)},\n            {\"ThreadSafeTeleportAck\", Value(threadSafeTeleportAck)},\n            {\"ThrownUsageFix\"" "[ConfigEcho], sorted (owner rule #615, #710)")

  # 2. Pure policy and its rules.
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc ${incs})
    if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>")
      list(APPEND errors "TeleportAckThreadPolicy.h is not pure: ${inc}")
    endif()
  endforeach()
  ban("${policy}" "sLog" "logging in the policy")
  need("${policy}" "    if (!on || !masterHolder || !inWorld)\n        return Acker::Holder;\n    if (nearTeleport)\n        return Acker::BotThread;\n    if (farTeleport)\n        return Acker::Wait;\n    return Acker::Holder;" "off / random holder / out of world = holder, near in world = bot thread, far in world = wait")
  need("${policy}" "    return requested && inWorld && nearTeleport;" "the bot thread acks only a request, in world, near pending")

  # 3. The holder: the old ack is the Holder case; the request and the wait do not ack; only PlayerbotMgr is a master holder.
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
  need("${sessions}" "        else if (acker == ai::teleport_ack::Acker::Wait)
        {
            ai::teleport_ack::Global().waits.fetch_add(1, std::memory_order_relaxed);
        }
        else if (GetBotAI(bot)" "Wait: no ack, before the old ack")
  string(REGEX MATCHALL "HandleTeleportAck\\(\\)" acks "${sessions}")
  list(LENGTH acks nacks)
  if(NOT nacks EQUAL 1)
    list(APPEND errors "UpdateSessions must ack in exactly one place (the Holder case), found ${nacks}")
  endif()
  need("${mgr_h}" "    virtual bool IsMasterHolder() const { return false; }" "the base holder (random bots) is not a master holder")
  need("${mgr_h}" "    Player* GetMaster() const { return master; };\n    bool IsMasterHolder() const override { return true; }" "PlayerbotMgr is the master holder")

  # 4. The bot thread: at the start of UpdateAI (after the inbox drain, before the park check), the flag taken once.
  need("${ai_cpp}" "#include \"playerbot/TeleportAckThreadPolicy.h\"" "policy include (bot)")
  need("${ai_h}" "    void RequestTeleportAck() { teleportAckRequested.store(true, std::memory_order_release); }" "the request sets the flag")
  need("${ai_h}" "    std::atomic<bool> teleportAckRequested{ false };" "atomic request flag, default false")
  between("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)" "    if (parked)" head)
  if(head STREQUAL "")
    list(APPEND errors "UpdateAI head (up to the park check) not found")
  endif()
  need("${head}" "    if (teleportAckRequested.exchange(false, std::memory_order_acq_rel) &&\n        ai::teleport_ack::BotThreadAcks(true, bot->IsInWorld(), bot->IsBeingTeleportedNear()))\n    {\n        HandleTeleportAck();" "the bot-thread ack before the park check, the flag taken once")
  before("${head}" "    HandleQueuedBotPackets();" "    if (teleportAckRequested.exchange(" "the inbox drain stays first")

  # 5. [WorldBots]: the suffix only when on, at the end, as increments.
  need("${rnd_cpp}" "        if (sPlayerbotAIConfig.threadSafeTeleportAck)\n        {\n            ai::teleport_ack::Counters& c = ai::teleport_ack::Global();" "suffix only when on")
  need("${rnd_cpp}" "std::to_string(c.requested.exchange(0))" "requested as increments")
  need("${rnd_cpp}" "teleport_ack_calls=%u online=%u%s\"," "the format unchanged (#597 pin)")
  need("${rnd_cpp}" "            (hook + tpAck).c_str());" "the suffix after the hook suffix")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${mgr_cpp}" "${mgr_h}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${rnd_cpp}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#563 X5 thread-safe teleport ack contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#563 X5 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${mgr_cpp}" "${mgr_h}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${rnd_cpp}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#563 X5 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.ThreadSafeTeleportAck = 0\n" "\nAiPlayerbot.ThreadSafeTeleportAck = 1\n")
probe("unknown = on" config_cpp "config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 0) == 1;" "config.GetIntDefault(\"AiPlayerbot.ThreadSafeTeleportAck\", 0) != 0;")
probe("echo missing" config_cpp "            {\"ThreadSafeTeleportAck\", Value(threadSafeTeleportAck)},\n" "")
probe("off defers" policy "    if (!on || !masterHolder || !inWorld)" "    if (!masterHolder || !inWorld)")
probe("random holder defers" policy "    if (!on || !masterHolder || !inWorld)" "    if (!on || !inWorld)")
probe("far handed over" policy "    if (farTeleport)\n        return Acker::Wait;" "    if (farTeleport)\n        return Acker::BotThread;")
probe("near kept on the holder" policy "    if (nearTeleport)\n        return Acker::BotThread;" "    if (nearTeleport)\n        return Acker::Holder;")
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

message(STATUS "teleport_ack_thread source contract passed (${probes} negative probes)")
