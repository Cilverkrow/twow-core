# twow-repo#627 S7 help groups (AiPlayerbot.QuestGroups.Enable, default 0): roster bots holding the same elite or group quest
# form a 2..5 bot group on the world thread, do it together, turn in and go back to their own travel. Pins the bounds agreed
# with OB-10 (world thread only, leader rule, timers, real players untouched), OB-20 (#601: no dungeon/raid, group loot) and
# CLI-Deep (lock-free member query, queue age, no band-trip interruption).

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
    message(FATAL_ERROR "#627 S7: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#627 S7: unexpected ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#627 S7: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_source("QuestGroupPolicy.h" policy)
read_source("QuestGroupWorld.cpp" world)
read_source("QuestGroupWorld.h" world_h)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAIConfig.cpp" config_source)
read_source("PlayerbotAIConfig.h" config_header)
read_source("aiplayerbot.conf.dist.in" config_template)
read_source("strategy/actions/LeaveGroupAction.cpp" leave)
read_source("strategy/actions/ShareQuestAction.cpp" share)

# Switch: default off, documented, echoed.
require_text("${config_header}" "uint32 questGroupsEnable = 0;" "member default off")
require_text("${config_source}" "config.GetIntDefault(\"AiPlayerbot.QuestGroups.Enable\", 0)" "key read, default 0")
require_text("${config_template}" "\nAiPlayerbot.QuestGroups.Enable = 0\n" "documented key")
require_text("${config_source}" "{\"QuestGroups.Enable\", Value(questGroupsEnable)}," "ConfigEcho entry")

# Policy: pure, no locks except the queue store, the agreed rules.
forbid_text("${policy}" "#include \"" "game includes in the policy")
forbid_text("${policy}" "sLog" "logging in the policy")
require_text("${policy}" "if (type == QuestTypeDungeon || type == QuestTypeRaid || type == QuestTypePvp)" "dungeon/raid/pvp out (OB-20)")
require_text("${policy}" "f.questLevel < f.botLevel + 5" "red quests stay out (owner 18.10.)")
require_text("${policy}" "return mode == ModeAll || (mode == ModeOnlyB && b) || (mode == ModeOnlyA && !b);" "cohort modes 0/1/2/3")
require_text("${policy}" "return a.openObjectives != b.openObjectives ? a.openObjectives > b.openObjectives : a.guid < b.guid;" "leader = most open objectives (OB-10)")
require_text("${policy}" "s.dissolve = Dissolve::NoProgress;   // NoProgress always fires: only objective progress resets it" "NoProgress always fires (CLI-Deep)")
require_text("${policy}" "if (f.realPlayer)            { s.dissolve = Dissolve::RealPlayer; return s; }" "real player first")

# World thread only: called from the roster branch before CheckBgFill, nowhere else.
require_text("${mgr}" "        if (sPlayerbotAIConfig.questGroupsEnable || ai::quest_groups::WorldManager().HasGroups())
            ai::quest_groups::WorldManager().Update(GetAllBots(), uint32(time(nullptr)));" "roster-branch hook")
string(REGEX MATCHALL "quest_groups::WorldManager\\(\\)\\.Update\\(" calls "${mgr}")
list(LENGTH calls n_calls)
if(NOT n_calls EQUAL 1)
  message(FATAL_ERROR "#627 S7: the manager must be updated from exactly one place, found ${n_calls}")
endif()

# Group APIs: the core paths, with the leader as inviter; no world scan; no quest log writes.
require_text("${world}" "leader->GetSession()->HandleGroupInviteOpcode(p);" "core invite path, leader invites")
require_text("${world}" "memberAi->DoSpecificAction(\"accept invitation\", Event(\"quest group\", \"\", leader), true);" "accept on the world thread")
require_text("${world}" "bot->GetSession()->HandleGroupDisbandOpcode(p);" "core leave path")
require_text("${world}" "group->SetLootMethod(GROUP_LOOT);" "group loot (OB-20 #601)")
require_text("${world}" "group->SetLootThreshold(ITEM_QUALITY_UNCOMMON);" "uncommon threshold")
forbid_text("${world}" "GetPartitions(" "the partition search (waits for a worker thread)")
forbid_text("${world}" "AddQuest" "quest log writes")
forbid_text("${world}" "RemoveQuest" "quest log writes")
forbid_text("${world}" "SetQuestStatus" "quest log writes")
forbid_text("${world}" "getQuestStatusMap()[" "quest status map inserts")
forbid_text("${world}" "DoSpecificAction(\"leave\"" "the self-leave that keeps a free bot in its group")

# Real players: never a candidate, a real master ends the group (contract pin, OB-10).
require_text("${world}" "!ai->HasRealPlayerMaster()" "candidates without a real master")
require_text("${world}" "if (ai->HasRealPlayerMaster())
            realPlayer = true;" "a real master ends the group")
require_text("${world}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())" "roster bots only")

# Forced travel only for the leader (and everyone in the turn-in phase), always with a condition (a forced target
# without one never ends); followers follow without travel.
require_text("${world}" "t->SetConditions({ cond });" "forced target with a condition")
require_text("${world}" "std::string const FollowerSet = \"-wander,+follow,-travel," "follower set")
require_text("${world}" "if (t->GetBandTripEnd())
            continue;   // a running S5a band trip is not interrupted (CLI-Deep 23.10.)" "band trips untouched")

# CLI-Deep queries: lock-free role atomic, queue age.
require_text("${ai_h}" "std::atomic<uint8> questGroupRole{ 0 };" "role atomic")
require_text("${world_h}" "std::uint32_t OldestQueuedAge(std::uint32_t guid);" "oldest queue age query")
require_text("${world}" "return ai && ai->GetQuestGroupRole() != 0;" "lock-free member query")

# Members are not dropped by "leave far away" and get no auto-shared quests.
require_text("${leave}" "        if (quest_groups::IsMember(ai))
            return false;" "leave far away spares help groups")
require_text("${share}" "return bot->GetGroup() && !ai->HasActivePlayerMaster() && !quest_groups::IsMember(ai);" "no auto share in help groups")

# Lines: one tag for all group lines (OB-20), cohort in every state line.
require_text("${world}" "[QuestGroup] state=formed group=%u quest=%u size=%u leader=%u levels=%u-%u objective=%d cohort=%c mixed=%u" "formed line")
require_text("${world}" "[QuestGroup] state=dissolved group=%u quest=%u size=%u reason=%s minutes=%u phase=%s turned_in=%u cohort=%c mixed=%u" "dissolved line")
require_text("${world}" "[QuestGroup] minute candidates=%u quests=%u formed=%u active=%u dissolved=%u queued=%u scan_us_max=%u passes=%u mode=%u" "minute line")

# Leader stuck: PosStuck never changes the map, so the CLI-Deep hook flags the leader lock-free and the manager takes it.
require_text("${ai_h}" "std::atomic<bool> questGroupLeaderStuck{ false };" "leader-stuck atomic")
require_text("${world}" "if (ai && ai->GetQuestGroupRole() == 2)
        ai->SetQuestGroupLeaderStuck(true);" "only a leader is flagged")
require_text("${world}" "f.leaderStuck = leaderAi && (approachStuck || leaderAi->TakeQuestGroupLeaderStuck() || leader->GetMapId() != g.mapId);" "manager takes the flag and its own approach watch")
require_text("${world}" "approachStuck = g.leaderWatch.Observe(" "the manager watches the forced leader itself (PosStuck skips forced targets, OB-10)")

# OB-10 anti-stillstand card R2: the upstream stuck triggers spare S7 members (the manager ends a stalled group); the move
# triggers restart their timer, move long stuck still fires for an invalid/unloaded grid (checked before the exemption).
read_source("strategy/triggers/StuckTriggers.h" stuck)
require_text("${policy}" "inline bool StuckTriggersExempt(std::uint8_t role, bool inGroup)
{
    return role != 0 && inGroup;
}" "R2 exemption: role and a real group")
require_text("${stuck}" "return quest_groups::StuckTriggersExempt(ai->GetQuestGroupRole(), bot->GetGroup() != nullptr);" "R2 helper")
set(r2_count 0)
set(r2_rest "${stuck}")
while(TRUE)
  string(FIND "${r2_rest}" "if (S7GroupMember(ai, bot))" r2_at)
  if(r2_at EQUAL -1)
    break()
  endif()
  math(EXPR r2_count "${r2_count} + 1")
  math(EXPR r2_at "${r2_at} + 10")
  string(SUBSTRING "${r2_rest}" ${r2_at} -1 r2_rest)
endwhile()
if(NOT r2_count EQUAL 4)
  message(FATAL_ERROR "quest group contract: R2 exemption in ${r2_count} stuck triggers, want 4 (move, move long, combat, combat long)")
endif()
require_order("${stuck}" "Stuck: In invalid grid" "RESET_AI_VALUE(uint32, \"experience\");
                return false;
            }

            uint32 timeSinceLastMove" "move long stuck: invalid grid before the R2 exemption")
