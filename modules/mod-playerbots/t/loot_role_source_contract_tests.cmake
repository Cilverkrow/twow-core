# twow-repo#308 LOOT-ROLE-01 (owner 15.10.2026, approved by OB-00): AiPlayerbot.Loot.RoleWeights (default 0) - weight scale
# from the premade path from level 1, the quest reward by gain; AiPlayerbot.Loot.FeralFixedSpec (default 1) - bear/cat
# by the path; [QuestReward] always on.

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
    message(FATAL_ERROR "#308 LOOT-ROLE-01: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#308 LOOT-ROLE-01: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#308 LOOT-ROLE-01: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#308 LOOT-ROLE-01: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#308 LOOT-ROLE-01: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomItemMgr.cpp" items)
read_source("strategy/actions/TalkToQuestGiverAction.cpp" talk)

# Switches: RoleWeights off, the feral fix (a correction of the 26.09. decision) on.
require_text("${config_h}" "bool lootRoleWeights = false;" "RoleWeights default off")
require_text("${config_cpp}" "lootRoleWeights = config.GetBoolDefault(\"AiPlayerbot.Loot.RoleWeights\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Loot.RoleWeights = 0" "documented off")
require_text("${config_h}" "bool lootFeralFixedSpec = true;" "FeralFixedSpec default on")
require_text("${config_cpp}" "lootFeralFixedSpec = config.GetBoolDefault(\"AiPlayerbot.Loot.FeralFixedSpec\", true);" "config default 1")
require_text("${conf_dist}" "AiPlayerbot.Loot.FeralFixedSpec = 1" "documented on")

# The weight scale: the premade path first, behind the switch; the talent tree stays the fallback.
between("${items}" "std::string RandomItemMgr::GetPlayerSpecName(Player* player)" "\n}\n" spec_name)
require_order("${spec_name}" "if (sPlayerbotAIConfig.lootRoleWeights)" "int tab = AiFactory::GetPlayerSpecTab(player);" "path before the tree")
require_text("${spec_name}" "std::string const scale = GetRoleSpecName(player);" "path scale")
require_text("${spec_name}" "ai::loot_role::FeralDps(sPlayerbotAIConfig.lootFeralFixedSpec, AiFactory::GetPremadePathName(player)," "bear/cat by the path")
forbid_text("${spec_name}" "if (player->GetLevel() > 19 && urand(0, 100) > 50)" "the coin outside the switch")

# The quest reward: the random-bot path picks through PickReward, the switch only in Pick, the line always on.
between("${talk}" "void TalkToQuestGiverAction::RewardMultipleItem(" "else if ((questRewardOption" auto_pick)
require_text("${auto_pick}" "uint32 const rewardIndex = PickReward(quest, bestIds);" "auto pick through PickReward")
require_text("${auto_pick}" "bot->RewardQuest(quest, rewardIndex, questGiver, true);" "the picked reward is given")
forbid_text("${auto_pick}" "RewardQuest(quest, *bestIds.begin()" "the first reward past PickReward")
between("${talk}" "uint32 TalkToQuestGiverAction::PickReward(" "\n}\n" pick)
require_text("${pick}" "ai::loot_role::Pick(sPlayerbotAIConfig.lootRoleWeights, first, best)" "the switch decides the pick")
require_text("${pick}" "uint32 const roleSpec = sRandomItemMgr.GetRoleSpecId(bot);" "scored with the role's scale")
require_order("${pick}" "ai::loot_role::Pick(" "sLog.outBasic(\"[QuestReward] bot=%u" "line after the pick")
string(FIND "${pick}" "if (sPlayerbotAIConfig.lootRoleWeights" gated)
if(NOT gated EQUAL -1)
  message(FATAL_ERROR "#308 LOOT-ROLE-01: the [QuestReward] line must not depend on the switch")
endif()

message(STATUS "loot_role source contract passed")
