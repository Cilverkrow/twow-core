cmake_policy(VERSION 3.16)
# L1 (owner 07.11.2026, AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly: code default 1, 0 = rollback, any other value = 1):
# bots reset their raid lockouts only on a real player's invite; a perm-bind split guard keeps a bot that kept its PERM raid
# binds out of another copy of that raid. Design ws-10/cli-grp/l1-lockout/DESIGN-L1-LOCKOUT-20261107.md (critic.md section 4).
# This contract pins:
#   - I0, the HARM invariant the core enforces (no core change): the player's own PERM bind is chosen before any group bind
#     (Player::GetBoundInstanceSaveForSelfOrGroup), CreateInstance takes that choice, the far ack builds its map with
#     CreateMap right before Map::Add, no instance-id TeleportTo exists, and the perm-mismatch MANGOS_ASSERT is still there;
#   - the module never bypasses it: no BindToInstance / CreateInstance / player Map::Add, UnbindInstance only in
#     ResetInstancesAction.cpp, "reset raids" only through ResetRaidsOnJoin in the accept tail;
#   - S1 (accept) and S3 (relay, teleport, summon, .bot summon) call the split guard behind the switch, the guard reads only
#     the bot's own binds and scalar ids;
#   - the switch (default 1, string parse, only "0" off, echo, conf.dist line), the [Lockout] minute line field order;
#   - leader_bind_carry (OB-00 07.11.2026, count only, both switch values): leader_carry_real / leader_carry_bot at the end of
#     the line, counted only through LeaderBindCarry, the group's and the leader's binds read only behind the world-thread
#     gate after the member's own check, the three PlayerScript hooks only note, count and forget (no behaviour change);
#     review fixes: the leader also when out of world (its own far teleport), the doc of the approximation (a group PERM
#     bind earned in the run counts too), and the core timing (OnMapChanged after Map::Add and only in the ack,
#     OnBeforeTeleport before the far branch).
# Pure rules: t/lockout_policy_tests.cpp. Every check_* returns "" when it holds, else the reason; the negative probes at
# the end reuse them on mutated text (the first ones are the negative test against the MANGOS_ASSERT path).
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "l1 lockout: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "l1 lockout: PB_MODULE_DIR is required")
endif()
if(NOT DEFINED CORE_SOURCE_DIR)
  set(CORE_SOURCE_DIR "${PB_MODULE_DIR}/../..")
endif()

set(TAG "l1 lockout")

function(read_file path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/LockoutPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/LockoutGuard.h" guard_h)
read_file("${PB_SOURCE_DIR}/LockoutGuard.cpp" guard_cpp)
read_file("${PB_SOURCE_DIR}/LockoutCarry.h" carry_h)
read_file("${PB_SOURCE_DIR}/LockoutCarry.cpp" carry_cpp)
read_file("${PB_SOURCE_DIR}/strategy/actions/AcceptInvitationAction.h" accept)
read_file("${PB_SOURCE_DIR}/strategy/actions/AreaTriggerAction.cpp" area)
read_file("${PB_SOURCE_DIR}/strategy/actions/TeleportAction.cpp" teleport)
read_file("${PB_SOURCE_DIR}/strategy/actions/UseMeetingStoneAction.cpp" meeting)
read_file("${PB_SOURCE_DIR}/strategy/actions/ResetInstancesAction.cpp" reset_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotMgr.cpp" mgr)
read_file("${PB_SOURCE_DIR}/PlayerbotScripts.cpp" scripts)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf_dist)
read_file("${PB_SOURCE_DIR}/GroupOps.cpp" ops_cpp)
read_file("${PB_SOURCE_DIR}/GroupOps.h" ops_h)
read_file("${PB_MODULE_DIR}/tests.cmake" tests_cmake)
read_file("${PB_MODULE_DIR}/t/lockout_policy_tests.cpp" unit_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/Player.cpp" player_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/Player.h" player_h)
read_file("${CORE_SOURCE_DIR}/src/game/Maps/MapManager.cpp" mapmgr_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Maps/Map.cpp" map_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Handlers/MovementHandler.cpp" movement_cpp)

# All module sources (ResetInstancesAction.cpp kept apart for the UnbindInstance rule).
file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(all_sources "")
set(all_but_reset "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(APPEND all_sources "${t}\n")
  if(NOT f MATCHES "/ResetInstancesAction\\.cpp$")
    string(APPEND all_but_reset "${t}\n")
  endif()
endforeach()

# The text from start up to and including the first stop after it ("" when start is missing).
function(region text start stop out)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(LENGTH "${start}" sl)
  string(SUBSTRING "${rest}" ${sl} -1 after)
  string(FIND "${after}" "${stop}" b)
  if(b EQUAL -1)
    set(${out} "${rest}" PARENT_SCOPE)
    return()
  endif()
  string(LENGTH "${stop}" tl)
  math(EXPR len "${sl} + ${b} + ${tl}")
  string(SUBSTRING "${rest}" 0 ${len} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# "" when first stands in text before second (both present), else the reason.
function(order text first second what out)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1)
    set(${out} "${what}: missing ${first}" PARENT_SCOPE)
  elseif(b EQUAL -1)
    set(${out} "${what}: missing ${second}" PARENT_SCOPE)
  elseif(NOT a LESS b)
    set(${out} "${what}: wrong order" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

# Number of matches of a regex (no ';' in the patterns: it would split the match list).
function(count_regex text pattern out)
  string(REGEX MATCHALL "${pattern}" m "${text}")
  list(LENGTH m n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# ---- I0 core pins ----------------------------------------------------------------------------------------------------------
# The perm-first guard in GetBoundInstanceSaveForSelfOrGroup: removing or inverting it lets a group bind win over the
# player's PERM bind, and DungeonMap::BindPlayerOrGroupOnEnter then reaches MANGOS_ASSERT(false).
function(check_core_choice player_cpp out)
  region("${player_cpp}" "DungeonPersistentState* Player::GetBoundInstanceSaveForSelfOrGroup(uint32 mapid)\n{" "\n}\n" body)
  if(body STREQUAL "")
    set(${out} "Player.cpp: GetBoundInstanceSaveForSelfOrGroup not found" PARENT_SCOPE)
    return()
  endif()
  order("${body}" "    InstancePlayerBind *pBind = GetBoundInstance(mapid);\n    DungeonPersistentState *state = pBind ? pBind->state : nullptr;"
    "    if (!pBind || !pBind->perm)\n    {\n        if (Group *group = GetGroup())" "Player.cpp perm-first guard" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${body}" "                state = groupBind->state;" g)
  string(REGEX MATCHALL "GetGroup\\(\\)" groups "${body}")
  list(LENGTH groups ng)
  if(g EQUAL -1 OR NOT ng EQUAL 1)
    set(${out} "Player.cpp: the group bind is read outside the perm-first guard" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_core_create mapmgr_cpp out)
  region("${mapmgr_cpp}" "Map* MapManager::CreateInstance(uint32 id, Player * player)\n{" "\n}\n" body)
  if(body STREQUAL "")
    set(${out} "MapManager.cpp: CreateInstance not found" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${body}" "    else if (DungeonPersistentState* pSave = player->GetBoundInstanceSaveForSelfOrGroup(id))" at)
  if(at EQUAL -1)
    set(${out} "MapManager.cpp: CreateInstance does not take GetBoundInstanceSaveForSelfOrGroup(id)" PARENT_SCOPE)
    return()
  endif()
  foreach(word "GetGroup" "GetBoundInstance(id" "->GetBoundInstance(")
    string(FIND "${body}" "${word}" w)
    if(NOT w EQUAL -1)
      set(${out} "MapManager.cpp: CreateInstance reads a bind of its own: ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_core_ack movement_cpp out)
  region("${movement_cpp}" "void WorldSession::HandleMoveWorldportAckOpcode()\n{" "\n}\n" body)
  if(body STREQUAL "")
    set(${out} "MovementHandler.cpp: HandleMoveWorldportAckOpcode() not found" PARENT_SCOPE)
    return()
  endif()
  order("${body}" "        map = sMapMgr.CreateMap(loc.mapId, GetPlayer());" "    GetPlayer()->SetMap(map);" "worldport ack" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  order("${body}" "    GetPlayer()->SetMap(map);" "GetPlayer()->GetMap()->Add(GetPlayer());" "worldport ack" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  count_regex("${body}" "->Add\\(GetPlayer\\(\\)\\)" n)
  if(NOT n EQUAL 1)
    set(${out} "worldport ack: ${n} Map::Add calls (1 expected)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# No way to name the instance copy in a teleport: no parameter called *InstanceId, no second integer right after the map id,
# exactly the three known Player.h overloads (map+coords, the AzerothCore long form, the location template) and one
# Player::TeleportTo definition in Player.cpp.
function(check_core_teleport player_h player_cpp out)
  string(REGEX MATCH "TeleportTo\\([^)]*[Ii]nstance[Ii]d" m "${player_h}")
  if(NOT m STREQUAL "")
    set(${out} "Player.h: a TeleportTo overload takes an instance id: ${m}" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCH "TeleportTo\\(uint32 [A-Za-z_]+, *(uint|int|ObjectGuid)" m "${player_h}")
  if(NOT m STREQUAL "")
    set(${out} "Player.h: a TeleportTo overload takes a second integer after the map id: ${m}" PARENT_SCOPE)
    return()
  endif()
  count_regex("${player_h}" "bool TeleportTo\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "Player.h: ${n} TeleportTo overloads (3 expected)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${player_cpp}" "Player::TeleportTo\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "Player.cpp: ${n} Player::TeleportTo definitions (1 expected)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_core_assert map_cpp out)
  region("${map_cpp}" "void DungeonMap::BindPlayerOrGroupOnEnter(Player* player)\n{" "\n}\n" body)
  if(body STREQUAL "")
    set(${out} "Map.cpp: BindPlayerOrGroupOnEnter not found" PARENT_SCOPE)
    return()
  endif()
  set(head "    InstancePlayerBind* playerBind = player->GetBoundInstance(GetId());\n    if (playerBind && playerBind->perm)\n    {\n        // cannot enter other instances if bound permanently\n        if (playerBind->state != GetPersistanceState())")
  # The assert inside the perm-mismatch block itself (the sibling asserts further down do not count).
  region("${body}" "${head}" "\n        }\n" perm_block)
  if(perm_block STREQUAL "")
    set(${out} "Map.cpp: the perm-mismatch block changed" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${perm_block}" "\n            MANGOS_ASSERT(false);\n" at)
  if(at EQUAL -1)
    set(${out} "Map.cpp: the perm-mismatch MANGOS_ASSERT is gone" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# leader_bind_carry core timing (review L2): OnMapChanged fires after the worldport ack's Map::Add (so after the core's bind on
# entry), the ack is its only call site in src/game (so no login count, no stale note read elsewhere: map_changed_sites), and
# OnBeforeTeleport fires in Player::TeleportTo before the far branch reads the binds and schedules the far teleport.
function(check_core_hooks movement_cpp player_cpp map_changed_sites out)
  region("${movement_cpp}" "void WorldSession::HandleMoveWorldportAckOpcode()\n{" "\n}\n" body)
  if(body STREQUAL "")
    set(${out} "MovementHandler.cpp: HandleMoveWorldportAckOpcode() not found" PARENT_SCOPE)
    return()
  endif()
  order("${body}" "->Add(GetPlayer());" "ForEachEnabledHook(PLAYERHOOK_ON_MAP_CHANGED," "OnMapChanged after the worldport ack's Map::Add" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  count_regex("${body}" "PLAYERHOOK_ON_MAP_CHANGED" n)
  if(NOT n EQUAL 1 OR NOT map_changed_sites EQUAL 1)
    set(${out} "PLAYERHOOK_ON_MAP_CHANGED: ${n} in the worldport ack, ${map_changed_sites} in src/game (1 and 1 expected: the ack is its only call site)" PARENT_SCOPE)
    return()
  endif()
  region("${player_cpp}" "bool Player::TeleportTo(uint32 mapid, float x, float y, float z, float orientation, uint32 options)\n{" "\n}\n" tp)
  if(tp STREQUAL "")
    set(${out} "Player.cpp: Player::TeleportTo not found" PARENT_SCOPE)
    return()
  endif()
  foreach(later "GetBoundInstanceSaveForSelfOrGroup(mapid);" "sMapMgr.ScheduleFarTeleport(this, data);")
    order("${tp}" "ForEachEnabledHook(PLAYERHOOK_ON_BEFORE_TELEPORT," "${later}" "OnBeforeTeleport before the far branch" r)
    if(NOT r STREQUAL "")
      set(${out} "${r}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- module: no bypass -----------------------------------------------------------------------------------------------------
function(check_module_binds all_sources all_but_reset reset_cpp out)
  foreach(word "BindToInstance(" "CreateInstance(")
    string(FIND "${all_sources}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "module calls ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # A player Map::Add (the pet add in PlayerbotFactory casts to Creature*).
  count_regex("${all_sources}" "[Mm]ap(\\(\\))?->Add\\([^(]" n)
  if(NOT n EQUAL 0)
    set(${out} "module adds a player to a map (${n} Map::Add calls)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${all_but_reset}" "UnbindInstance" u)
  if(NOT u EQUAL -1)
    set(${out} "UnbindInstance outside ResetInstancesAction.cpp" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${reset_cpp}" "bot->UnbindInstance(itr);" r)
  if(r EQUAL -1)
    set(${out} "ResetInstancesAction.cpp: the reset raids unbind is missing" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# "reset raids" is reached only through ResetRaidsOnJoin in JoinTail. Only the bot-only managers (ad-hoc, quest groups) skip
# the inviter read; the group op (BOT-12 queue, A3 lever 2) and the posted tail can carry a real player's invite.
function(check_join_tail accept all_sources out)
  region("${accept}" "        void JoinTail(Group* grp, Player* inviter, Event& event, bool onBotThread = false)" "\n        }\n" tail)
  if(tail STREQUAL "")
    set(${out} "AcceptInvitationAction.h: JoinTail not found" PARENT_SCOPE)
    return()
  endif()
  set(gate "            std::string const& l1Source = event.getSource();
            bool const l1BotManager = group_ai_refresh::IsWorldManagerSource(l1Source) && l1Source != group_ai_refresh::GroupOpSource;
            bool const l1InviterReal = !l1BotManager && ::IsRealPlayer(inviter);
            bool const resetRaids = ai::lockout::ResetRaidsOnJoin(sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly,
                l1BotManager, false, l1InviterReal);
            ai::lockout_guard::CountJoin(resetRaids, l1InviterReal);
            if (resetRaids)
                ai->DoSpecificAction(\"reset raids\", event, true);
            ai->DoSpecificAction(\"update gear\", event, true);")
  string(FIND "${tail}" "${gate}" at)
  if(at EQUAL -1)
    set(${out} "JoinTail: reset raids is not gated by ResetRaidsOnJoin (or the update gear line changed)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "DoSpecificAction\\(\"reset raids\"" n)
  if(NOT n EQUAL 1)
    set(${out} "DoSpecificAction(\"reset raids\" called ${n} times in the module (1 expected, JoinTail)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# The pure rules.
function(check_policy policy out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${policy}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include <(cstdint|string)>$")
      set(${out} "LockoutPolicy.h: not a pure include: ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(word "sPlayerbotAIConfig" "Player*" "Group*" "sLog")
    string(FIND "${policy}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "LockoutPolicy.h: names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "    if (self.instanceId && self.perm)\n        return self.instanceId;\n    if (group.instanceId)\n        return group.instanceId;\n    return self.instanceId;"
      "    return self.instanceId && self.perm && self.instanceId != entered;"
      "    return selfOnM.instanceId && selfOnM.perm && targetOnM && targetOnM != selfOnM.instanceId;"
      "    return !switchOn || (!worldManagerSource && !joinTailSource && inviterReal);"
      "    if (first == std::string::npos)\n        return true;"
      "    return raw.compare(first, last - first + 1, \"0\") != 0;"
      "    return copy && b.perm && b.instanceId == copy;"
      "    return PermOn(selfAfter, entered) && !(selfBefore.instanceId && selfBefore.perm);"
      "    return !e.memberIsLeader && e.leaderIsBot && EntryPermFromGroup(e.selfBefore, e.selfAfter, e.entered) &&\n        PermOn(e.groupOnM, e.entered) && PermOn(e.leaderOnM, e.entered);"
      "        return resetReal || resetBot || keepBot || splitDecline || splitEntry || leaderCarryReal || leaderCarryBot;")
    string(FIND "${policy}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "LockoutPolicy.h: rule changed: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# The guard reads the bot's own binds and scalar ids only; the split test is LockSplit on a dungeon map.
function(check_guard guard_cpp out)
  foreach(needle
      "    if (InstancePlayerBind const* pBind = bot->GetBoundInstance(mapId))"
      "    if (!entry || !entry->IsDungeon())\n        return false;\n    return lockout::LockSplit(OwnBind(bot, mapId), targetInstanceId);"
      "    if (target->GetMapId() != mapId)"
      "    return SplitToward(bot, mapId, target->GetInstanceId());"
      "    if (!bot || !target || target == bot || !target->IsBeingTeleportedFar())"
      "    if (target->GetTeleportDest().mapId != mapId || target->GetMapId() == mapId)\n        return false;\n    MapEntry const* entry = sMapStorage.LookupEntry<MapEntry>(mapId);\n    if (!entry || !entry->IsDungeon())\n        return false;\n    lockout::Bind const own = OwnBind(bot, mapId);\n    return own.instanceId && own.perm;")
    string(FIND "${guard_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "LockoutGuard.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(word "GetGroup" "m_boundInstances" "GetBoundInstanceSaveForSelfOrGroup" "GetBoundInstances" "target->GetBound" "BindToInstance")
    string(FIND "${guard_cpp}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "LockoutGuard.cpp: reads a foreign or group bind: ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_regex("${guard_cpp}" "GetBoundInstance\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "LockoutGuard.cpp: ${n} GetBoundInstance reads (1 expected, the bot's own)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# The counters: the enum, its size kCounters and the Take() list of MinuteTick move together (a counter without a Take line
# would never print).
function(check_counters guard_h guard_cpp out)
  string(FIND "${guard_h}" "enum Counter\n{\n    ResetReal = 0,\n    ResetBot,\n    KeepBot,\n    SplitDecline,\n    SplitEntry,\n    LeaderCarryReal,   // leader_bind_carry (LockoutCarry.cpp), the entering member a real player\n    LeaderCarryBot,    // leader_bind_carry (LockoutCarry.cpp), the entering member a bot\n    kCounters,\n};" at)
  if(at EQUAL -1)
    set(${out} "LockoutGuard.h: the Counter enum changed (update kCounters, MinuteTick and this pin together)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${guard_cpp}" "std::array<std::atomic<std::uint32_t>, kCounters> counters{};" at)
  if(at EQUAL -1)
    set(${out} "LockoutGuard.cpp: the counter array is not sized by kCounters" PARENT_SCOPE)
    return()
  endif()
  region("${guard_cpp}" "void MinuteTick(std::uint32_t diff)\n{" "\n}\n" tick)
  string(FIND "${tick}" "    c.resetReal = Take(ResetReal);\n    c.resetBot = Take(ResetBot);\n    c.keepBot = Take(KeepBot);\n    c.splitDecline = Take(SplitDecline);\n    c.splitEntry = Take(SplitEntry);\n    c.leaderCarryReal = Take(LeaderCarryReal);\n    c.leaderCarryBot = Take(LeaderCarryBot);\n" at)
  if(at EQUAL -1)
    set(${out} "LockoutGuard.cpp: MinuteTick does not take every counter in enum order" PARENT_SCOPE)
    return()
  endif()
  count_regex("${tick}" "Take\\(" n)
  if(NOT n EQUAL 7)
    set(${out} "LockoutGuard.cpp: MinuteTick takes ${n} counters (7 expected, one per enum value)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# S1: after the security decline, before the core accept, behind the switch, the same decline (through the write path
# like the security decline when A3 lever 2 routes the bot; the group op is not a bot-only manager).
function(check_s1 accept out)
  set(block "            if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly)
            {
                bool const l1BotManager = group_ai_refresh::IsWorldManagerSource(event.getSource()) &&
                    event.getSource() != group_ai_refresh::GroupOpSource;
                bool const l1InviterReal = !l1BotManager && ::IsRealPlayer(inviter);
                if (!ai::lockout::ResetRaidsOnJoin(true, l1BotManager, false, l1InviterReal) &&
                    ai::lockout_guard::SplitToward(bot, inviter->GetMapId(), inviter->GetInstanceId()))
                {
                    ai::lockout_guard::Count(ai::lockout_guard::SplitDecline);
                    // A3 lever 2 (BotGroups.WritePath): an in-scope bot declines on the world thread, like the security decline.
                    if (group_ops::WritePathRoutes(ai) && !group_ai_refresh::IsWorldManagerSource(event.getSource()) &&
                        group_ops::QueueAccept(ai, inviter->GetGUIDLow(), true))
                        return false;
                    WorldPacket data(SMSG_GROUP_DECLINE, 10);
                    data << bot->GetName();
                    sServerFacade.SendPacket(inviter, data);
                    bot->UninviteFromGroup();
                    return false;
                }
            }")
  string(FIND "${accept}" "${block}" at)
  if(at EQUAL -1)
    set(${out} "S1: the switch-gated split decline is missing or changed" PARENT_SCOPE)
    return()
  endif()
  order("${accept}" "PLAYERBOT_SECURITY_INVITE" "${block}" "S1 after the security decline" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  order("${accept}" "${block}" "bot->GetSession()->HandleGroupAcceptOpcode(p);" "S1 before the core accept" r)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# S3: each module-initiated move into a raid map, behind the switch, counted.
function(check_s3 area teleport meeting mgr all_sources out)
  set(cnt "\n    {\n        ai::lockout_guard::Count(ai::lockout_guard::SplitEntry);")
  region("${area}" "bool ReachAreaTriggerAction::Execute(Event& event)" "\n}\n" reach)
  order("${reach}" "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelaySplit(bot, triggerId, requester))${cnt}"
    "    if (bot->GetMapId() != atEntry->mapid ||" "S3 ReachAreaTriggerAction" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  region("${area}" "bool AreaTriggerAction::Execute(Event& event)" "\n}\n" relay)
  order("${relay}" "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelaySplit(bot, triggerId, GetMaster()))${cnt}"
    "    bot->GetSession()->HandleAreaTriggerOpcode(p);" "S3 AreaTriggerAction relay" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # S3 timing (review A L-1): the relay keeps the trigger while the master is still on its far teleport toward M, after the
  # consume and before the split check, behind the switch.
  set(wait "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelayWait(bot, triggerId, GetMaster()))\n    {\n        movement.lastAreaTrigger = triggerId;\n        return false;\n    }")
  order("${relay}" "    movement.lastAreaTrigger = 0;" "${wait}" "S3 relay wait after the consume" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  order("${relay}" "${wait}" "ai::lockout_guard::RelaySplit(bot, triggerId, GetMaster())" "S3 relay wait before the split check" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "lockout_guard::RelayWait\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "lockout_guard::RelayWait( called ${n} times (1 expected: the relay)" PARENT_SCOPE)
    return()
  endif()
  # Review A L-2: the refused teleport consumes the trigger (no second count, no relay toward another target).
  order("${teleport}" "        if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelaySplit(bot, movement.lastAreaTrigger, requester))\n        {\n            ai::lockout_guard::Count(ai::lockout_guard::SplitEntry);\n            movement.lastAreaTrigger = 0;"
    "        bot->GetSession()->HandleAreaTriggerOpcode(p);" "S3 TeleportAction relay (refusal consumes the trigger)" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  order("${meeting}" "    if (player == bot && summoner != player && sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly &&\n        ai::lockout_guard::SplitToward(bot, summoner->GetMapId(), summoner->GetInstanceId()))${cnt}"
    "                if (!player->TeleportTo(mapId, x, y, z, 0))" "S3 SummonAction" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  region("${mgr}" "std::string PlayerbotHolder::HandleBotSummon" "std::string PlayerbotHolder::HandleBotRemoveLogout" summon)
  order("${summon}" "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly &&\n        ai::lockout_guard::SplitToward(bot, master->GetMapId(), master->GetInstanceId()))${cnt}"
    "    bot->TeleportTo(master->GetMapId()," "S3 .bot summon" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # No other caller: S1 + SummonAction + .bot summon, and the three relays.
  count_regex("${all_sources}" "lockout_guard::SplitToward\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "lockout_guard::SplitToward( called ${n} times (3 expected: S1, SummonAction, .bot summon)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "lockout_guard::RelaySplit\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "lockout_guard::RelaySplit( called ${n} times (3 expected: reach, relay, teleport)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# The switch: member default 1, string parse with "1", echo at its sorted place, conf.dist = 1 once.
function(check_switch config_h config_cpp conf_dist out)
  string(FIND "${config_h}" "    bool botGroupsResetRaidsRealInviterOnly = true;" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.h: member not default 1" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${config_cpp}" "    botGroupsResetRaidsRealInviterOnly = ai::lockout::SwitchOn(config.GetStringDefault(\"AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly\", \"1\"));" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.cpp: not a string parse with default \"1\" through SwitchOn" PARENT_SCOPE)
    return()
  endif()
  count_regex("${config_cpp}" "AiPlayerbot\\.BotGroups\\.ResetRaidsRealInviterOnly" n)
  if(NOT n EQUAL 1)
    set(${out} "PlayerbotAIConfig.cpp: the key is read ${n} times (1 expected)" PARENT_SCOPE)
    return()
  endif()
  set(echo "            {\"BotGroups.OpQueueOpsPerTick\", Value(botGroupsOpQueueOpsPerTick)},\n            {\"BotGroups.ResetRaidsRealInviterOnly\", Value(botGroupsResetRaidsRealInviterOnly)},\n            {\"BotGroups.WorldThreadDefer\"")
  string(FIND "${config_cpp}" "${echo}" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.cpp: the echo entry is missing or not at its sorted place" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "\nAiPlayerbot\\.BotGroups\\.ResetRaidsRealInviterOnly = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1)
    set(${out} "conf.dist: the key is documented ${n} times (1 expected)" PARENT_SCOPE)
    return()
  endif()
  if(NOT lines STREQUAL "\nAiPlayerbot.BotGroups.ResetRaidsRealInviterOnly = 1")
    set(${out} "conf.dist: the key is not 1" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "# Blind spots:" "# Out of scope:" "core-only summons" "a missing minute means")
    string(FIND "${conf_dist}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "conf.dist: the doc block lacks ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# The minute line: field order, only with a non-zero field, from the world thread, not in GroupOps.
function(check_minute policy guard_cpp scripts all_sources ops out)
  set(prev -1)
  foreach(field "\"[Lockout] minute mode=\"" "\" reset_real=\"" "\" reset_bot=\"" "\" keep_bot=\"" "\" split_decline=\"" "\" split_entry=\""
      "\" leader_carry_real=\"" "\" leader_carry_bot=\"")
    string(FIND "${policy}" "${field}" at)
    if(at EQUAL -1 OR NOT at GREATER prev)
      set(${out} "LockoutPolicy.h: minute line field order changed at ${field}" PARENT_SCOPE)
      return()
    endif()
    set(prev ${at})
  endforeach()
  order("${guard_cpp}" "    if (!c.Any())" "sLog.outBasic(\"%s\", lockout::MinuteLine(" "minute line only with a counted field" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  region("${scripts}" "        void OnUpdate(uint32 diff) override" "\n        }\n" on_update)
  order("${on_update}" "            if (!sPlayerbotAIConfig.enabled)\n                return;\n\n            ai::lockout_guard::MinuteTick(diff);" "sRandomPlayerbotMgr.RunQueuedRemoteCommands();" "MinuteTick in the world OnUpdate" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "lockout_guard::MinuteTick\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "lockout_guard::MinuteTick( called ${n} times (1 expected)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${ops}" "lockout" g)
  if(NOT g EQUAL -1)
    set(${out} "GroupOps files name lockout (lever 2 owns them)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_unit_test tests_cmake unit_cpp out)
  foreach(needle "add_executable(lockout_policy_tests\n  \"\${PB_MODULE_DIR}/t/lockout_policy_tests.cpp\")" "add_test(NAME lockout_policy\n  COMMAND lockout_policy_tests")
    string(FIND "${tests_cmake}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "tests.cmake: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # Review B 1: the body too - the I0 loop over every bind combination, the forced-copy negative case, the LockSplit and
  # ResetRaidsOnJoin truth tables.
  foreach(needle
      "    for (std::uint32_t id : {0u, 7u, 9u})\n    {\n        for (bool perm : {false, true})"
      "    for (Bind const& self : selves)\n        for (Bind const& group : groups)\n            Require(!PermAssertWouldFire(self, CoreChoice(self, group)), \"I0: the core's own choice never fires the perm assert\");"
      "    Require(PermAssertWouldFire({7, true}, 9), \"perm self forced into another copy fires\");"
      "    Require(LockSplit({7, true}, 9), \"perm on M, target in another copy: split\");"
      "    Require(!LockSplit({7, true}, 7), \"perm on M, target in the same copy: no split\");"
      "    for (int bits = 0; bits < 8; ++bits)"
      "        Require(ResetRaidsOnJoin(false, wm, tail, real), \"switch 0: reset raids on every accept\");"
      "        Require(ResetRaidsOnJoin(true, wm, tail, real) == (!wm && !tail && real), \"switch 1: only a real inviter on the direct path\");")
    string(FIND "${unit_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "lockout_policy_tests.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# leader_bind_carry (OB-00 07.11.2026, count only): the note at the far teleport, the decision at the end of the worldport ack
# (the member's own check first, the group's and the leader's binds only behind the world-thread gate, the count only through
# LeaderBindCarry), the hooks only note, count and forget, nothing in LockoutCarry.cpp changes the game.
function(check_carry carry_h carry_cpp scripts conf_dist unit_cpp all_sources out)
  foreach(needle
      "    if (!player || mapId == player->GetMapId())   // a near teleport: no map entry follows\n        return;"
      "    if (!IsDungeonMap(mapId))\n    {\n        std::lock_guard<std::mutex> lock(notesLock);\n        notes.erase(guid);\n        return;\n    }"
      "    note.before = PlayerBindOn(player, mapId);"
      "    if (note.mapId != mapId || !IsDungeonMap(mapId))\n        return;"
      "    if (InstancePlayerBind const* pBind = player->GetBoundInstance(mapId))"
      "    if (InstanceGroupBind const* gBind = group->GetBoundInstance(mapId))")
    string(FIND "${carry_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "LockoutCarry.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  region("${carry_cpp}" "void OnEntry(Player* player)\n{" "\n}\n" entry)
  if(entry STREQUAL "")
    set(${out} "LockoutCarry.cpp: OnEntry not found" PARENT_SCOPE)
    return()
  endif()
  # The order inside OnEntry: take the note, the member's own check, the world-thread gate, the foreign reads, the count.
  # (Quoted foreach items: the steps hold ';', which a list would split.)
  set(prev -1)
  foreach(step
    "        notes.erase(it);"
    "    e.selfAfter = PlayerBindOn(player, mapId);\n    if (!lockout::EntryPermFromGroup(e.selfBefore, e.selfAfter, e.entered))\n        return;"
    "    if (!World::IsWorldThread())\n        return;"
    "    Group* group = player->GetGroup();"
    "    Player* leader = e.memberIsLeader ? player : ObjectAccessor::FindPlayerNotInWorld(leaderGuid);"
    "    e.leaderIsBot = !::IsRealPlayer(leader);"
    "    e.leaderOnM = PlayerBindOn(leader, mapId);"
    "    e.groupOnM = GroupBindOn(group, mapId);"
    "    if (lockout::LeaderBindCarry(e))\n        lockout_guard::Count(::IsRealPlayer(player) ? lockout_guard::LeaderCarryReal : lockout_guard::LeaderCarryBot);\n}")
    string(FIND "${entry}" "${step}" at)
    if(at EQUAL -1 OR NOT at GREATER prev)
      set(${out} "LockoutCarry.cpp OnEntry: missing or out of order: ${step}" PARENT_SCOPE)
      return()
    endif()
    set(prev ${at})
  endforeach()
  # Before the gate only the member's own data: no group, no leader, no other player.
  string(FIND "${entry}" "    if (!World::IsWorldThread())" gate)
  string(SUBSTRING "${entry}" 0 ${gate} before_gate)
  foreach(word "GetGroup" "GroupBindOn(" "sObjectMgr" "ObjectAccessor" "leader" "IsRealPlayer")
    string(FIND "${before_gate}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "LockoutCarry.cpp OnEntry: ${word} before the world-thread gate" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_regex("${carry_cpp}" "World::IsWorldThread\\(\\)" n)
  count_regex("${carry_cpp}" "GetGroup\\(\\)" ng)
  count_regex("${carry_cpp}" "GroupBindOn\\(group" nb)
  if(NOT n EQUAL 1 OR NOT ng EQUAL 1 OR NOT nb EQUAL 1)
    set(${out} "LockoutCarry.cpp: one world-thread gate, one group read, one group bind read expected (${n} ${ng} ${nb})" PARENT_SCOPE)
    return()
  endif()
  # Count only: one Count call, the two carry counters only there, no write, no switch.
  count_regex("${carry_cpp}" "lockout_guard::Count\\(" n)
  count_regex("${all_sources}" "lockout_guard::LeaderCarry(Real|Bot)" nc)
  if(NOT n EQUAL 1 OR NOT nc EQUAL 2)
    set(${out} "leader_bind_carry: counted outside the LeaderBindCarry line (${n} Count calls, ${nc} counter names)" PARENT_SCOPE)
    return()
  endif()
  foreach(word "BindToInstance" "Unbind" "TeleportTo" "SendPacket" "Uninvite" "RemoveMember" "ChangeLeader" "SetLeader"
      "Disband" "ResetInstances" "DoSpecificAction" "SetSemaphore" "->Add(" "->Remove(" "botGroupsResetRaidsRealInviterOnly")
    string(FIND "${carry_cpp}${carry_h}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "LockoutCarry: not count only, names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # The hooks: each only hands the player over, gated by the module switch, not by the L1 switch.
  foreach(needle
      "        void OnBeforeTeleport(Player* player, uint32 mapId, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/) override\n        {\n            if (!player || !sPlayerbotAIConfig.enabled)\n                return;\n\n            ai::lockout_carry::NoteTeleport(player, mapId);\n        }"
      "        void OnMapChanged(Player* player) override\n        {\n            if (!player || !sPlayerbotAIConfig.enabled)\n                return;\n\n            ai::lockout_carry::OnEntry(player);\n        }"
      "        void OnLogout(Player* player) override\n        {\n            if (!player)\n                return;\n\n            ai::lockout_carry::Forget(player);\n        }")
    string(FIND "${scripts}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotScripts.cpp: leader_bind_carry hook changed: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(call "lockout_carry::NoteTeleport\\(" "lockout_carry::OnEntry\\(" "lockout_carry::Forget\\(")
    count_regex("${all_sources}" "${call}" n)
    if(NOT n EQUAL 1)
      set(${out} "${call} called ${n} times (1 expected, its hook)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # Review M1: the count names its approximation (a group PERM bind earned in the run counts too); review L1: only an offline
  # leader is skipped, a leader on its own far teleport is still read.
  foreach(needle "# leader_bind_carry (OB-00 07.11.2026) is count only: no rule, no behaviour change, the same at 0 and 1"
      "with the raid ADR, O7)" " split_entry= leader_carry_real= leader_carry_bot=`"
      "The origin of the group's PERM bind is not visible at entry, so both count"
      "and one the group earned in the current run (a boss kill with the leader"
      "an offline leader (a leader that is itself on a")
    string(FIND "${conf_dist}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "conf.dist: the leader_bind_carry doc lacks ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "// Origin (review M1): the entry cannot see where the group's PERM bind came from."
      "and one the group earned in the current run (DungeonMap::PermBindAllPlayers binds the"
      "// a leader that is offline (a leader that is itself on a far teleport, out of world, is still read behind the gate).")
    string(FIND "${carry_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "LockoutCarry.h: the leader_bind_carry doc lacks ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "    Require(!EntryPermFromGroup({7, true}, {7, true}, 7), \"own PERM bind on the copy before: never counted\");"
      "    Require(LeaderBindCarry(carry), \"bot leader's carried PERM bind, member new on the copy: counted\");"
      "        Require(!LeaderBindCarry(e), \"the leader itself is never counted\");"
      "        Require(!LeaderBindCarry(e), \"a real (or unknown) leader is not counted\");"
      "        Require(!LeaderBindCarry(e), \"the member's own pre-existing PERM bind on the copy is not counted\");"
      "        Require(LeaderBindCarry(e), \"a group PERM bind earned in the run (released member, solo bind on the copy before): counted too\");"
      "                        Require(CoreChoice(before, group) == entered && !PermAssertWouldFire(before, entered),"
      "leader_carry_real=6 leader_carry_bot=7\",")
    string(FIND "${unit_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "lockout_policy_tests.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- run on the real sources -----------------------------------------------------------------------------------------------
set(ops "${ops_cpp}\n${ops_h}")
check_core_choice("${player_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_core_create("${mapmgr_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_core_ack("${movement_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_core_teleport("${player_h}" "${player_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_core_assert("${map_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
# Call sites of PLAYERHOOK_ON_MAP_CHANGED in src/game (the enum in ScriptObjects.h is not one).
file(GLOB_RECURSE core_game_files "${CORE_SOURCE_DIR}/src/game/*.cpp" "${CORE_SOURCE_DIR}/src/game/*.h")
set(map_changed_sites 0)
foreach(f IN LISTS core_game_files)
  if(f MATCHES "/ScriptObjects\\.h$")
    continue()
  endif()
  file(STRINGS "${f}" hits REGEX "PLAYERHOOK_ON_MAP_CHANGED")
  if(NOT hits STREQUAL "")
    string(REGEX MATCHALL "PLAYERHOOK_ON_MAP_CHANGED" m "${hits}")
    list(LENGTH m n)
    math(EXPR map_changed_sites "${map_changed_sites} + ${n}")
  endif()
endforeach()
check_core_hooks("${movement_cpp}" "${player_cpp}" "${map_changed_sites}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_module_binds("${all_sources}" "${all_but_reset}" "${reset_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_join_tail("${accept}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_policy("${policy}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_guard("${guard_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_counters("${guard_h}" "${guard_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_s1("${accept}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_s3("${area}" "${teleport}" "${meeting}" "${mgr}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_minute("${policy}" "${guard_cpp}" "${scripts}" "${all_sources}" "${ops}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_unit_test("${tests_cmake}" "${unit_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_carry("${carry_h}" "${carry_cpp}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()

# ---- negative probes: each mutation must be caught -------------------------------------------------------------------------
function(expect_caught result description)
  if(result STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${description}")
  endif()
endfunction()

# The MANGOS_ASSERT path: a probe copy of Player.cpp without the perm-first guard, and one with it inverted, would let the
# group bind win over a PERM bind and fire the assert in BindPlayerOrGroupOnEnter.
string(REPLACE "    if (!pBind || !pBind->perm)\n    {\n        if (Group *group = GetGroup())" "    {\n        if (Group *group = GetGroup())" m "${player_cpp}")
check_core_choice("${m}" r)
expect_caught("${r}" "perm-first guard removed (MANGOS_ASSERT path)")

string(REPLACE "    if (!pBind || !pBind->perm)\n" "    if (!pBind || pBind->perm)\n" m "${player_cpp}")
check_core_choice("${m}" r)
expect_caught("${r}" "perm-first guard inverted (MANGOS_ASSERT path)")

string(REPLACE "    DungeonPersistentState *state = pBind ? pBind->state : nullptr;\n" "    DungeonPersistentState *state = pBind ? pBind->state : nullptr;\n    if (GetGroup() && GetGroup()->GetBoundInstance(mapid))\n        state = GetGroup()->GetBoundInstance(mapid)->state;\n" m "${player_cpp}")
check_core_choice("${m}" r)
expect_caught("${r}" "group bind read before the perm-first guard (MANGOS_ASSERT path)")

string(REPLACE "            MANGOS_ASSERT(false);\n        }\n    }\n    else" "        }\n    }\n    else" m "${map_cpp}")
check_core_assert("${m}" r)
expect_caught("${r}" "perm-mismatch MANGOS_ASSERT removed")

string(REPLACE "    else if (DungeonPersistentState* pSave = player->GetBoundInstanceSaveForSelfOrGroup(id))" "    else if (DungeonPersistentState* pSave = player->GetGroup() ? player->GetGroup()->GetBoundInstance(id)->state : player->GetBoundInstanceSaveForSelfOrGroup(id))" m "${mapmgr_cpp}")
check_core_create("${m}" r)
expect_caught("${r}" "CreateInstance uses the group bind first")

string(REPLACE "        map = sMapMgr.CreateMap(loc.mapId, GetPlayer());" "        map = sMapMgr.FindMap(loc.mapId, GetPlayer()->GetInstanceId());" m "${movement_cpp}")
check_core_ack("${m}" r)
expect_caught("${r}" "worldport ack without CreateMap")

# Review B 4: the ack adds the player before CreateMap picked the copy.
string(REPLACE "    bool const _scAddOk = GetPlayer()->GetMap()->Add(GetPlayer());" "    bool const _scAddOk = true;" m "${movement_cpp}")
string(REPLACE "        map = sMapMgr.CreateMap(loc.mapId, GetPlayer());" "        GetPlayer()->GetMap()->Add(GetPlayer());\n        map = sMapMgr.CreateMap(loc.mapId, GetPlayer());" m "${m}")
check_core_ack("${m}" r)
expect_caught("${r}" "worldport ack adds the player before CreateMap")

check_core_teleport("${player_h}\n        bool TeleportTo(uint32 mapid, uint32 instanceId, float x, float y, float z, float orientation);" "${player_cpp}" r)
expect_caught("${r}" "an instance-id TeleportTo added")

# Review B 2: other parameter names, another shape, a Player.cpp-only definition.
check_core_teleport("${player_h}\n        bool TeleportTo(uint32 mapid, uint32 copy, float x, float y, float z, float orientation);" "${player_cpp}" r)
expect_caught("${r}" "a TeleportTo with a second integer after the map id")

check_core_teleport("${player_h}\n        bool TeleportTo(uint32 mapid, float x, float y, float z, float orientation, uint32 options, uint32 copy);" "${player_cpp}" r)
expect_caught("${r}" "a fourth TeleportTo overload")

check_core_teleport("${player_h}" "${player_cpp}\nbool Player::TeleportTo(WorldLocation const& loc, uint32 copy)\n{\n}\n" r)
expect_caught("${r}" "a second Player::TeleportTo definition in Player.cpp")

check_module_binds("${all_sources}\n    bot->BindToInstance(state, true);" "${all_but_reset}" "${reset_cpp}" r)
expect_caught("${r}" "a module BindToInstance")

# Review B 4: a module CreateInstance.
check_module_binds("${all_sources}\n    Map* map = sMapMgr.CreateInstance(mapId, bot);" "${all_but_reset}" "${reset_cpp}" r)
expect_caught("${r}" "a module CreateInstance")

check_module_binds("${all_sources}\n    bot->GetMap()->Add(bot);" "${all_but_reset}" "${reset_cpp}" r)
expect_caught("${r}" "a module player Map::Add")

check_module_binds("${all_sources}" "${all_but_reset}\n    bot->UnbindInstance(itr);" "${reset_cpp}" r)
expect_caught("${r}" "UnbindInstance outside ResetInstancesAction.cpp")

string(REPLACE "            if (resetRaids)\n                ai->DoSpecificAction(\"reset raids\", event, true);" "            ai->DoSpecificAction(\"reset raids\", event, true);" m "${accept}")
check_join_tail("${m}" "${all_sources}" r)
expect_caught("${r}" "an unconditional reset raids")

check_join_tail("${accept}" "${all_sources}\n    ai->DoSpecificAction(\"reset raids\", event, true);" r)
expect_caught("${r}" "a second reset raids call site")

string(REPLACE "            bool const l1BotManager = group_ai_refresh::IsWorldManagerSource(l1Source) && l1Source != group_ai_refresh::GroupOpSource;" "            bool const l1BotManager = group_ai_refresh::IsWorldManagerSource(l1Source);" m "${accept}")
check_join_tail("${m}" "${all_sources}" r)
expect_caught("${r}" "the group op (a real player's invite through the queue) treated as a bot-only manager in JoinTail")

string(REPLACE "bool const l1InviterReal = !l1BotManager && ::IsRealPlayer(inviter);\n            bool const resetRaids" "bool const l1InviterReal = false;\n            bool const resetRaids" m "${accept}")
check_join_tail("${m}" "${all_sources}" r)
expect_caught("${r}" "JoinTail never reads the inviter")

string(REPLACE "                bool const l1BotManager = group_ai_refresh::IsWorldManagerSource(event.getSource()) &&\n                    event.getSource() != group_ai_refresh::GroupOpSource;" "                bool const l1BotManager = group_ai_refresh::IsWorldManagerSource(event.getSource());" m "${accept}")
check_s1("${m}" r)
expect_caught("${r}" "the group op treated as a bot-only manager in S1")

string(REPLACE "                    if (group_ops::WritePathRoutes(ai) && !group_ai_refresh::IsWorldManagerSource(event.getSource()) &&\n                        group_ops::QueueAccept(ai, inviter->GetGUIDLow(), true))\n                        return false;\n                    WorldPacket data(SMSG_GROUP_DECLINE, 10);" "                    WorldPacket data(SMSG_GROUP_DECLINE, 10);" m "${accept}")
check_s1("${m}" r)
expect_caught("${r}" "S1 decline bypasses the write path")

string(REPLACE "    return !switchOn || (!worldManagerSource && !joinTailSource && inviterReal);" "    return !switchOn || inviterReal;" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "ResetRaidsOnJoin ignores the world-manager sources")

string(REPLACE "    return selfOnM.instanceId && selfOnM.perm && targetOnM && targetOnM != selfOnM.instanceId;" "    return false;" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "LockSplit disabled")

string(REPLACE "    if (InstancePlayerBind const* pBind = bot->GetBoundInstance(mapId))" "    if (InstanceGroupBind const* pBind = bot->GetGroup()->GetBoundInstance(mapId))" m "${guard_cpp}")
check_guard("${m}" r)
expect_caught("${r}" "the guard reads a group bind")

string(REPLACE "            if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly)\n            {\n                bool const l1BotManager" "            {\n                bool const l1BotManager" m "${accept}")
check_s1("${m}" r)
expect_caught("${r}" "S1 without the switch")

string(REPLACE "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly &&\n        ai::lockout_guard::SplitToward(bot, master->GetMapId(), master->GetInstanceId()))" "    if (ai::lockout_guard::SplitToward(bot, master->GetMapId(), master->GetInstanceId()))" m "${mgr}")
check_s3("${area}" "${teleport}" "${meeting}" "${m}" "${all_sources}" r)
expect_caught("${r}" "S3 .bot summon without the switch")

string(REPLACE "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelaySplit(bot, triggerId, GetMaster()))" "    if (ai::lockout_guard::RelaySplit(bot, triggerId, GetMaster()))" m "${area}")
check_s3("${m}" "${teleport}" "${meeting}" "${mgr}" "${all_sources}" r)
expect_caught("${r}" "S3 relay without the switch")

check_s3("${area}" "${teleport}" "${meeting}" "${mgr}" "${all_sources}\n    if (ai::lockout_guard::SplitToward(bot, 1, 2)) {}" r)
expect_caught("${r}" "an ungated fourth SplitToward call")

# Review A L-1: the relay wait removed, ungated, or put before the consume.
string(REPLACE "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelayWait(bot, triggerId, GetMaster()))\n    {\n        movement.lastAreaTrigger = triggerId;\n        return false;\n    }\n" "" m "${area}")
check_s3("${m}" "${teleport}" "${meeting}" "${mgr}" "${all_sources}" r)
expect_caught("${r}" "S3 relay without the landing wait")

string(REPLACE "    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelayWait(bot, triggerId, GetMaster()))" "    if (ai::lockout_guard::RelayWait(bot, triggerId, GetMaster()))" m "${area}")
check_s3("${m}" "${teleport}" "${meeting}" "${mgr}" "${all_sources}" r)
expect_caught("${r}" "S3 relay wait without the switch")

string(REPLACE "    uint32 triggerId = movement.lastAreaTrigger;\n    movement.lastAreaTrigger = 0;\n" "    uint32 triggerId = movement.lastAreaTrigger;\n" m "${area}")
check_s3("${m}" "${teleport}" "${meeting}" "${mgr}" "${all_sources}" r)
expect_caught("${r}" "S3 relay without the consume before the wait")

string(REPLACE "    lockout::Bind const own = OwnBind(bot, mapId);\n    return own.instanceId && own.perm;" "    return true;" m "${guard_cpp}")
check_guard("${m}" r)
expect_caught("${r}" "the relay wait for every bot, not only a perm-bound one")

# Review A L-2: the refused teleport keeps the trigger (second count, relay toward another target).
string(REPLACE "            ai::lockout_guard::Count(ai::lockout_guard::SplitEntry);\n            movement.lastAreaTrigger = 0;\n" "            ai::lockout_guard::Count(ai::lockout_guard::SplitEntry);\n" m "${teleport}")
check_s3("${area}" "${m}" "${meeting}" "${mgr}" "${all_sources}" r)
expect_caught("${r}" "S3 teleport refusal keeps the trigger")

# Review B 3: a counter without a Take line, a Take line dropped.
string(REPLACE "    kCounters,\n};" "    SplitOther,\n    kCounters,\n};" m "${guard_h}")
check_counters("${m}" "${guard_cpp}" r)
expect_caught("${r}" "a new counter without a Take line")

string(REPLACE "    c.splitDecline = Take(SplitDecline);\n" "" m "${guard_cpp}")
check_counters("${guard_h}" "${m}" r)
expect_caught("${r}" "a counter never taken in MinuteTick")

string(REPLACE "    bool botGroupsResetRaidsRealInviterOnly = true;" "    bool botGroupsResetRaidsRealInviterOnly = false;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 0")

string(REPLACE "\"AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly\", \"1\")" "\"AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly\", \"0\")" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 0")

string(REPLACE "ai::lockout::SwitchOn(config.GetStringDefault(\"AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly\", \"1\"))" "config.GetIntDefault(\"AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly\", 1) != 0" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "atoi parse (\"on\" would be 0)")

string(REPLACE "    return raw.compare(first, last - first + 1, \"0\") != 0;" "    return raw != \"off\";" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "switch parse no longer only \"0\" off")

string(REPLACE "\nAiPlayerbot.BotGroups.ResetRaidsRealInviterOnly = 1" "\nAiPlayerbot.BotGroups.ResetRaidsRealInviterOnly = 0" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "conf.dist default 0")

string(REPLACE "            {\"BotGroups.ResetRaidsRealInviterOnly\", Value(botGroupsResetRaidsRealInviterOnly)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no echo entry")

string(REPLACE "        \" keep_bot=\" + std::to_string(c.keepBot) +\n        \" split_decline=\" + std::to_string(c.splitDecline) +" "        \" split_decline=\" + std::to_string(c.splitDecline) +\n        \" keep_bot=\" + std::to_string(c.keepBot) +" m "${policy}")
check_minute("${m}" "${guard_cpp}" "${scripts}" "${all_sources}" "${ops}" r)
expect_caught("${r}" "a field order swap")

string(REPLACE "    if (!c.Any())" "    if (false)" m "${guard_cpp}")
check_minute("${policy}" "${m}" "${scripts}" "${all_sources}" "${ops}" r)
expect_caught("${r}" "minute line printed with every field 0")

check_minute("${policy}" "${guard_cpp}" "${scripts}" "${all_sources}" "${ops}\n    ai::lockout_guard::Count(ai::lockout_guard::KeepBot);" r)
expect_caught("${r}" "lockout counters in GroupOps")

string(REPLACE "add_test(NAME lockout_policy\n" "add_test(NAME other\n" m "${tests_cmake}")
check_unit_test("${m}" "${unit_cpp}" r)
expect_caught("${r}" "unit test not registered")

# Review B 1: the unit test body gutted.
string(REPLACE "            Require(!PermAssertWouldFire(self, CoreChoice(self, group)), \"I0: the core's own choice never fires the perm assert\");" "            (void)self, (void)group;" m "${unit_cpp}")
check_unit_test("${tests_cmake}" "${m}" r)
expect_caught("${r}" "unit test without the I0 loop check")

string(REPLACE "    Require(PermAssertWouldFire({7, true}, 9), \"perm self forced into another copy fires\");\n" "" m "${unit_cpp}")
check_unit_test("${tests_cmake}" "${m}" r)
expect_caught("${r}" "unit test without the forced-copy negative case")

string(REPLACE "        Require(ResetRaidsOnJoin(true, wm, tail, real) == (!wm && !tail && real), \"switch 1: only a real inviter on the direct path\");" "" m "${unit_cpp}")
check_unit_test("${tests_cmake}" "${m}" r)
expect_caught("${r}" "unit test without the ResetRaidsOnJoin table at switch 1")

# leader_bind_carry (OB-00 07.11.2026, count only): field order, the predicate gate, the thread gate, no behaviour change.
string(REPLACE "        \" leader_carry_real=\" + std::to_string(c.leaderCarryReal) +\n        \" leader_carry_bot=\" + std::to_string(c.leaderCarryBot);" "        \" leader_carry_bot=\" + std::to_string(c.leaderCarryBot) +\n        \" leader_carry_real=\" + std::to_string(c.leaderCarryReal);" m "${policy}")
check_minute("${m}" "${guard_cpp}" "${scripts}" "${all_sources}" "${ops}" r)
expect_caught("${r}" "leader_carry_bot before leader_carry_real")

string(REPLACE "        \" split_entry=\" + std::to_string(c.splitEntry) +\n        \" leader_carry_real=\" + std::to_string(c.leaderCarryReal) +" "        \" leader_carry_real=\" + std::to_string(c.leaderCarryReal) +\n        \" split_entry=\" + std::to_string(c.splitEntry) +" m "${policy}")
check_minute("${m}" "${guard_cpp}" "${scripts}" "${all_sources}" "${ops}" r)
expect_caught("${r}" "leader_carry_real moved before split_entry (not appended at the end)")

string(REPLACE "        \" leader_carry_bot=\" + std::to_string(c.leaderCarryBot);" "        \"\";" m "${policy}")
check_minute("${m}" "${guard_cpp}" "${scripts}" "${all_sources}" "${ops}" r)
expect_caught("${r}" "leader_carry_bot missing from the line")

string(REPLACE "    c.leaderCarryBot = Take(LeaderCarryBot);\n" "" m "${guard_cpp}")
check_counters("${guard_h}" "${m}" r)
expect_caught("${r}" "the leader_carry_bot counter never taken")

string(REPLACE " || leaderCarryReal || leaderCarryBot;" ";" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "a minute with only a carry not printed")

string(REPLACE "    if (lockout::LeaderBindCarry(e))\n" "" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "carry counted without the LeaderBindCarry gate")

string(REPLACE "    return PermOn(selfAfter, entered) && !(selfBefore.instanceId && selfBefore.perm);" "    return PermOn(selfAfter, entered);" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "a member's own pre-existing PERM bind counted")

string(REPLACE "    return !e.memberIsLeader && e.leaderIsBot && EntryPermFromGroup(" "    return !e.memberIsLeader && EntryPermFromGroup(" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "LeaderBindCarry without the bot leader condition")

string(REPLACE "PermOn(e.groupOnM, e.entered) && PermOn(e.leaderOnM, e.entered);" "PermOn(e.groupOnM, e.entered);" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "LeaderBindCarry without the leader's own PERM bind")

string(REPLACE "    if (!World::IsWorldThread())\n        return;\n" "" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "group and leader binds read without the world-thread gate")

string(REPLACE "    lockout::CarryEntry e;\n" "    lockout::CarryEntry e;\n    Group* early = player->GetGroup();\n" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "a group read before the world-thread gate")

string(REPLACE "    if (!lockout::EntryPermFromGroup(e.selfBefore, e.selfAfter, e.entered))\n        return;\n" "" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "foreign reads without the member's own check first")

string(REPLACE "        notes.erase(it);\n" "" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "the note not taken at the entry (counted again)")

string(REPLACE "    if (!player || mapId == player->GetMapId())   // a near teleport: no map entry follows" "    if (!player)" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "a near teleport noted")

check_carry("${carry_h}" "${carry_cpp}\n    player->TeleportTo(mapId, 0.0f, 0.0f, 0.0f, 0.0f);" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "LockoutCarry.cpp teleports (behaviour change)")

check_carry("${carry_h}" "${carry_cpp}\n    group->ChangeLeader(player->GetObjectGuid());" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "LockoutCarry.cpp changes the group (behaviour change)")

check_carry("${carry_h}" "${carry_cpp}\n    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly) {}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "LockoutCarry.cpp reads the L1 switch (counts must not depend on it)")

string(REPLACE "            if (!player || !sPlayerbotAIConfig.enabled)\n                return;\n\n            ai::lockout_carry::OnEntry(player);" "            if (!player || !sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly)\n                return;\n\n            ai::lockout_carry::OnEntry(player);" m "${scripts}")
check_carry("${carry_h}" "${carry_cpp}" "${m}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "the entry hook gated by the L1 switch")

string(REPLACE "            ai::lockout_carry::OnEntry(player);\n" "            ai::lockout_carry::OnEntry(player);\n            player->UninviteFromGroup();\n" m "${scripts}")
check_carry("${carry_h}" "${carry_cpp}" "${m}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "the entry hook does more than count")

check_carry("${carry_h}" "${carry_cpp}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}\n    ai::lockout_guard::Count(ai::lockout_guard::LeaderCarryReal);" r)
expect_caught("${r}" "a second, ungated carry count")

string(REPLACE "# leader_bind_carry (OB-00 07.11.2026) is count only" "# leader_bind_carry (OB-00 07.11.2026) is a rule" m "${conf_dist}")
check_carry("${carry_h}" "${carry_cpp}" "${scripts}" "${m}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "conf.dist does not name leader_bind_carry as count only")

string(REPLACE "        Require(!LeaderBindCarry(e), \"the member's own pre-existing PERM bind on the copy is not counted\");\n" "" m "${unit_cpp}")
check_carry("${carry_h}" "${carry_cpp}" "${scripts}" "${conf_dist}" "${m}" "${all_sources}" r)
expect_caught("${r}" "unit test without the pre-existing PERM bind case")

# Review fixes (carry-review.md): M1 doc, L1 leader lookup, L2 core timing.
string(REPLACE "The origin of the group's PERM bind is not visible at entry, so both count" "Only a carried bind counts" m "${conf_dist}")
check_carry("${carry_h}" "${carry_cpp}" "${scripts}" "${m}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "conf.dist hides that a group PERM bind earned in the run counts too")

string(REPLACE "// Origin (review M1): the entry cannot see where the group's PERM bind came from." "// Origin: carried binds only." m "${carry_h}")
check_carry("${m}" "${carry_cpp}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "LockoutCarry.h hides that a group PERM bind earned in the run counts too")

string(REPLACE "ObjectAccessor::FindPlayerNotInWorld(leaderGuid)" "sObjectMgr.GetPlayer(leaderGuid)" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "the leader looked up in world only (a leader on its own far teleport missed)")

string(REPLACE "    lockout::CarryEntry e;\n" "    lockout::CarryEntry e;\n    Player* early = ObjectAccessor::FindPlayerNotInWorld(ObjectGuid());\n" m "${carry_cpp}")
check_carry("${carry_h}" "${m}" "${scripts}" "${conf_dist}" "${unit_cpp}" "${all_sources}" r)
expect_caught("${r}" "another player looked up before the world-thread gate")

set(map_changed_hook "    ScriptRegistry<PlayerScript>::ForEachEnabledHook(PLAYERHOOK_ON_MAP_CHANGED, [&](PlayerScript* script)\n    {\n        script->OnMapChanged(GetPlayer());\n    });\n")
string(REPLACE "${map_changed_hook}" "" m "${movement_cpp}")
string(REPLACE "    bool const _scAddOk = GetPlayer()->GetMap()->Add(GetPlayer());\n" "${map_changed_hook}    bool const _scAddOk = GetPlayer()->GetMap()->Add(GetPlayer());\n" m "${m}")
check_core_hooks("${m}" "${player_cpp}" 1 r)
expect_caught("${r}" "OnMapChanged moved before the worldport ack's Map::Add (permanent 0)")

check_core_hooks("${movement_cpp}" "${player_cpp}" 2 r)
expect_caught("${r}" "a second PLAYERHOOK_ON_MAP_CHANGED call site in src/game (e.g. at login)")

set(before_tp_hook "    ScriptRegistry<PlayerScript>::ForEachEnabledHook(PLAYERHOOK_ON_BEFORE_TELEPORT, [&](PlayerScript* script)\n    {\n        script->OnBeforeTeleport(this, mapid, x, y, z, orientation);\n    });\n")
string(REPLACE "${before_tp_hook}" "" m "${player_cpp}")
string(REPLACE "        sMapMgr.ScheduleFarTeleport(this, data);\n" "        sMapMgr.ScheduleFarTeleport(this, data);\n${before_tp_hook}" m "${m}")
check_core_hooks("${movement_cpp}" "${m}" 1 r)
expect_caught("${r}" "OnBeforeTeleport moved after the far branch (stale note)")

string(REPLACE "        Require(LeaderBindCarry(e), \"a group PERM bind earned in the run (released member, solo bind on the copy before): counted too\");\n" "" m "${unit_cpp}")
check_carry("${carry_h}" "${carry_cpp}" "${scripts}" "${conf_dist}" "${m}" "${all_sources}" r)
expect_caught("${r}" "unit test without the earned-in-run case")

message(STATUS "${TAG}: OK")
