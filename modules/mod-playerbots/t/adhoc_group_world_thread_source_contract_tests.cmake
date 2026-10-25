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

# twow-repo#365 HelpUntilDone (owner 25.10.2026, default 0): whoever is done follows an open member until the group is done.
read_source("aiplayerbot.conf.dist.in" config_template)
require_text("${config_source}" "botGroupsAdhocHelpUntilDone = config.GetBoolDefault(\"AiPlayerbot.BotGroups.AdHoc.HelpUntilDone\", false);" "HelpUntilDone key, default 0")
require_text("${config_template}" "AiPlayerbot.BotGroups.AdHoc.HelpUntilDone = 0" "HelpUntilDone documented")
require_text("${manager}" "facts.helpUntilDone = sPlayerbotAIConfig.botGroupsAdhocHelpUntilDone;" "the switch reaches the leave facts")
require_text("${manager}" "if (HelpsGroup(facts) && helped)" "helper only with an open member to follow")
require_text("${manager}" "            ai->SetQuestGroupRole(1);
            ai->SetMaster(helped);
            ai->RequestGroupAi(group_ai_refresh::Follower);" "the follower set on the bot's own thread, on every change")
require_text("${manager}" "[BotGroup] kind=adhoc event=help bot=%u group=%u quest=%u objective=%u follows=%u first=%u thread=world" "help line")

# OB-10 review #660 B1: the helper role goes on every way out - the manager's own helper set, never found by role (S7
# followers carry role 1 too), a sweep of every helper not seen helping in the pass, and the release after a reload to 0.
require_text("${manager_header}" "std::unordered_map<std::uint32_t, std::uint32_t> helpers;" "own helper set")
require_text("${manager}" "        helpingPass.insert(bot->GetGUIDLow());" "a helper is marked when seen helping")
require_text("${manager}" "    if (helpers.erase(bot->GetGUIDLow()))
    {
        ai->SetQuestGroupRole(0);
        ai->SetMaster(nullptr);
    }" "the ad-hoc leave drops the role of its own helpers only")
forbid_text("${manager}" "GetQuestGroupRole() != 0" "a helper found by its role")
forbid_text("${manager}" "GetQuestGroupRole() == 1" "a helper found by its role")
require_text("${manager}" "    if (pass.next >= pass.guids.size())
    {" "the pass end")
require_order("${manager}" "    if (pass.next >= pass.guids.size())
    {" "        SweepHelpers(bots);
        helpingPass.clear();
        Pairs(bots, now);" "the sweep once per pass after the leave rules of all slices (every exit path seen), before the pairs")
string(FIND "${manager}" "void WorldThreadManager::ScanSlice(" ss_a)
string(FIND "${manager}" "void WorldThreadManager::Pairs(" ss_b)
if(ss_a EQUAL -1 OR ss_b EQUAL -1 OR NOT ss_a LESS ss_b)
  message(FATAL_ERROR "#628 ad-hoc world thread: ScanSlice before Pairs not found")
endif()
math(EXPR ss_len "${ss_b} - ${ss_a}")
string(SUBSTRING "${manager}" ${ss_a} ${ss_len} scan_slice_body)
forbid_text("${scan_slice_body}" "SweepHelpers" "a sweep per slice (it would release the helpers of the other slices)")
require_text("${manager}" "        facts.s7Owns = quest_groups::OwnsBot(it->first);" "S7 probe before the role is touched")
require_order("${manager}" "        HelperExit const exit = DecideHelperExit(facts);" "            ai->SetQuestGroupRole(0);
        if (exit == HelperExit::Release)" "the decision before any role change")
require_text("${manager}" "        if (exit == HelperExit::RoleOnly || exit == HelperExit::Release)
            ai->SetQuestGroupRole(0);" "role 0 only on RoleOnly and Release (Forget = S7 or logged out: untouched)")
require_text("${manager}" "            ai->RequestGroupAi(group ? group_ai_refresh::JoinReset : group_ai_refresh::LeaveReset);" "release: the joined or the left strategies back")
require_text("${manager}" "[BotGroup] kind=adhoc event=help_end bot=%u group=%u exit=%s in_group=%u thread=world" "help_end line")
require_text("${mgr}" "        else if (ai::adhoc_group::WorldManager().HasHelpers())
            ai::adhoc_group::WorldManager().ReleaseHelpers(GetAllBots());" "helpers released after a reload to 0")
read_source("QuestGroupWorld.h" s7_header)
read_source("QuestGroupWorld.cpp" s7_manager)
require_text("${s7_header}" "bool Owns(std::uint32_t guid) const { return memberOf.count(guid) != 0 || staged.count(guid) != 0; }" "S7 ownership: its members and the bots in its StagedOps queues")
require_text("${s7_manager}" "bool OwnsBot(std::uint32_t guid)
{
    return WorldManager().Owns(guid);
}" "S7 ownership query")
