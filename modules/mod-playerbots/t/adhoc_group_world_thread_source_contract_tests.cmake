# twow-repo#628 (AiPlayerbot.BotGroups.AdHoc.WorldThread, default 0): the ad-hoc quest groups (#365) form and leave
# in the roster manager on the world thread, not in the bots' map-thread action. The core handles the group opcodes
# on the world thread (PACKET_PROCESS_GROUP = PACKET_PROCESS_WORLD); Group::Create/Disband write the global group map
# without a lock. Also: a manager leave really leaves (the action's self-leave was a no-op for a free bot), and
# "leave far away" no longer drops ad-hoc members.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#628 ad-hoc world thread: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#628 ad-hoc world thread: unexpected ${description}: ${needle}")
  endif()
endfunction()

read_source("AdhocGroupWorld.cpp" manager)
read_source("AdhocGroupWorld.h" manager_header)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("strategy/actions/AdhocGroupAction.cpp" action)
read_source("strategy/actions/LeaveGroupAction.cpp" leave)
read_source("PlayerbotAIConfig.cpp" config_source)
read_source("PlayerbotAIConfig.h" config_header)
read_source("aiplayerbot.conf.dist.in" config_template)
read_source("strategy/actions/AcceptInvitationAction.h" accept)
read_source("PlayerbotScripts.cpp" scripts)

if(NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "#628 ad-hoc world thread: CORE_SOURCE_DIR is required")
endif()
file(READ "${CORE_SOURCE_DIR}/src/game/World.cpp" world)
file(READ "${CORE_SOURCE_DIR}/src/game/Maps/MapManager.cpp" map_manager)
file(READ "${CORE_SOURCE_DIR}/src/game/WorldSession.h" session)
file(READ "${CORE_SOURCE_DIR}/src/game/Group/Group.cpp" group_cpp)
string(REPLACE "\r" "" world "${world}")
string(REPLACE "\r" "" map_manager "${map_manager}")
string(REPLACE "\r" "" session "${session}")
string(REPLACE "\r" "" group_cpp "${group_cpp}")

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#628 ad-hoc world thread: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

# Why the world thread (OB-10 review): the core handles group opcodes there, and the manager runs when no map
# thread updates a bot - the world script hook fires after sMapMgr.Update, which returns only after the instance
# pool and continents.wait(). The roster branch of UpdateAIInternal is reached from that hook (CheckBgFill, #517).
require_text("${session}" "PACKET_PROCESS_GROUP = PACKET_PROCESS_WORLD," "group opcodes are world-thread opcodes")
require_order("${world}" "    sMapMgr.Update(diff);" "ForEachEnabledHook(WORLDHOOK_ON_UPDATE" "world script hook after the map update")
require_order("${map_manager}" "continents.wait();" "asyncMapUpdating = false;" "map update returns after the continents")
require_text("${scripts}" "sRandomPlayerbotMgr.UpdateAI(diff);" "bot manager driven by the world script")

# The invitee's queued SMSG_GROUP_INVITE event later runs "accept invitation" with no pending invite: the accept
# cleared it (Group::RemoveInvite -> SetGroupInvite(nullptr)), and the action returns before any side effect.
require_text("${group_cpp}" "player->SetGroupInvite(nullptr);" "accept clears the pending invite")
require_text("${accept}" "            Group* grp = bot->GetGroupInvite();
            if (!grp)
                return false;" "accept without an invite is a no-op")

# Registry stays bounded: entries of groups that are gone are forgotten every pass.
require_text("${manager}" "Groups().ForgetGone([](std::uint32_t groupId) { return sObjectMgr.GetGroupById(groupId) != nullptr; });" "registry cleanup")

# Key, default off, documented.
require_text("${config_header}" "bool botGroupsAdhocWorldThread = false;" "member default off")
require_text("${config_source}" "config.GetBoolDefault(\"AiPlayerbot.BotGroups.AdHoc.WorldThread\", false)" "key read, default off")
require_text("${config_template}" "AiPlayerbot.BotGroups.AdHoc.WorldThread = 0" "documented key")

# Called from the roster branch next to CheckBgFill (world thread, maps idle), only with both switches.
require_text("${mgr}" "            CheckBgFill();
        // twow-repo#628:" "hook right after CheckBgFill")
require_text("${mgr}" "if (sPlayerbotAIConfig.botGroupsEnabled && sPlayerbotAIConfig.botGroupsAdhocWorldThread)
            ai::adhoc_group::WorldManager().Update(GetAllBots(), uint32(time(nullptr)));" "gated manager call")

# The map-thread action steps aside when the manager runs.
require_text("${action}" "return sPlayerbotAIConfig.botGroupsEnabled && !sPlayerbotAIConfig.botGroupsAdhocWorldThread &&" "action off with the manager")

# "leave far away" leaves ad-hoc groups alone when the manager runs.
require_text("${leave}" "if (sPlayerbotAIConfig.botGroupsAdhocWorldThread && adhoc_group::Groups().Find(bot->GetGroup()->GetId(), adhocEntry))
            return false;" "leave far away skips ad-hoc groups")

# Same rules as the action: one key per bot, objective buckets, radius, cooldown, level window, who invites.
require_text("${manager}" "ObjectiveKey const key = inAdhoc ? entry.key : CurrentObjective(bot);" "objective key per bot")
require_text("${manager}" "buckets[BucketKey(bot, key)].push_back(" "objective buckets")
require_text("${manager}" "sServerFacade.GetDistance2d(a.bot, b.bot) > radius" "radius")
require_text("${manager}" "PairCooldowns().IsBlocked(a.bot->GetGUIDLow(), b.bot->GetGUIDLow(), now)" "pair cooldown")
require_text("${manager}" "PairCooldowns().Block(a.bot->GetGUIDLow(), b.bot->GetGUIDLow(), now + cooldown, now);" "cooldown on invite")
require_text("${manager}" "LevelWindowOk(minLevel, maxLevel, sPlayerbotAIConfig.botGroupsLevelWindow)" "level window")
require_text("${manager}" "Invite const invite = Decide(" "who invites")
require_text("${manager}" "break;  // at most one invite per bot and pass" "one invite per bot and pass")
require_text("${manager}" "Leave const reason = DecideLeave(facts);" "design 3.2 leave rules")
forbid_text("${manager}" "GetPlayers()" "a world scan")
forbid_text("${manager}" "AddQuest" "quest log changes")
forbid_text("${manager}" "RemoveQuest" "quest log changes")

# Join on the world thread: invite, the invitee's own accept path, register, free-for-all.
require_text("${manager}" "inviter->GetSession()->HandleGroupInviteOpcode(p);" "core invite path")
require_text("${manager}" "inviteeAi->DoSpecificAction(\"accept invitation\", Event(\"adhoc group\", \"\", inviter), true);" "accept on the world thread")
require_text("${manager}" "Groups().Register(group->GetId(), key, now);" "registry")
require_text("${manager}" "group->SetLootMethod(FREE_FOR_ALL);" "free-for-all loot")

# A leave really leaves: the core leave path, not the action's self-leave (ShouldStayInGroup keeps a free bot).
require_text("${manager}" "bot->GetSession()->HandleGroupDisbandOpcode(p);" "core leave path")
forbid_text("${manager}" "DoSpecificAction(\"leave\"" "the self-leave that keeps a free bot in its group")

# Visible: thread=world on the lines, a minute line with counters for the watch.
require_text("${manager}" "[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u thread=world" "leave line")
require_text("${manager}" "[BotGroup] kind=adhoc event=minute passes=%u candidates=%u invites=%u joins=%u leaves=%u tracked=%u pass_us_max=%u" "minute line")

# Manager state is world-thread only (no per-tick lock).
require_text("${manager_header}" "std::unordered_map<std::uint32_t, Track> tracks;  // world thread only" "world-thread state")
