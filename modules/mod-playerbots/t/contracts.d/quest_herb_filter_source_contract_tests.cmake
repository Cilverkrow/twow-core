# twow-repo#333 Berufe P1-f (owner 25.10.2026 via OB-00): AiPlayerbot.Gather.QuestHerbFilter, modes 0 / 1 / 2 (in-realm A/B),
# default 0, ConfigEcho. A quest herb (lock 259: herbalism next to "open kneeling") is picked only while one of the bot's quests
# wants it; the gather travel targets leave it out since #414 (IsGatherNode), unchanged.

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
    message(FATAL_ERROR "#333 QuestHerbFilter: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#333 QuestHerbFilter: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 QuestHerbFilter: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("GatherNodePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("LootObjectStack.cpp" loot_cpp)
read_source("TravelMgr.cpp" travel_mgr)

# Switch: a mode, default 0, unknown -> 0, documented, echoed.
require_text("${config_h}" "uint32 gatherQuestHerbFilter = 0;" "default off, a mode")
require_text("${config_cpp}" "gatherQuestHerbFilter = config.GetIntDefault(\"AiPlayerbot.Gather.QuestHerbFilter\", 0);" "config read")
require_text("${config_cpp}" "if (gatherQuestHerbFilter > 2)\n        gatherQuestHerbFilter = 0;" "unknown mode is off")
require_text("${config_cpp}" "{\"Gather.QuestHerbFilter\", Value(gatherQuestHerbFilter)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.Gather.QuestHerbFilter = 0\n" "documented, 0 by default")
forbid_text("${conf_dist}" "\nAiPlayerbot.Gather.QuestHerbFilter = 1" "default 1 in the dist")
forbid_text("${conf_dist}" "\nAiPlayerbot.Gather.QuestHerbFilter = 2" "default 2 in the dist")

# Pure rule: the "open kneeling" slot marks a quest herb, not the raw lock skill 0 (Peacebloom / Silverleaf have it too).
forbid_text("${policy}" "#include \"playerbot/" "game headers in the pure policy")
require_text("${policy}" "return lockHasHerbalism && lockHasNonProfessionSkill;" "quest herb = herbalism + non-profession slot")
require_text("${policy}" "return !questHerb || activeForQuest;" "a quest herb only with its quest")

# Passive pick: the lock walk as in #414, the cohort first, the quest check only for a quest herb, after the active-quest branch.
require_text("${loot_cpp}" "#include \"playerbot/GatherNodePolicy.h\"" "policy include")
require_text("${loot_cpp}" "#include \"playerbot/CohortPolicy.h\"" "cohort include")
require_text("${loot_cpp}" "if (lockInfo->Type[i] != LOCK_KEY_SKILL || !lockInfo->Index[i])" "skill slots only")
require_text("${loot_cpp}" "if (skillId == SKILL_HERBALISM)\n            lockHasHerbalism = true;\n        else if (skillId == 0)\n            lockHasNonProfessionSkill = true;" "herbalism and non-profession slots")
require_text("${loot_cpp}" "if (skillId == SKILL_HERBALISM && ai::cohort::Applies(sPlayerbotAIConfig.gatherQuestHerbFilter, bot->GetGUIDLow()))" "switch and cohort")
require_text("${loot_cpp}" "if (!ai::gather_node::QuestHerbLootable(questHerb, questHerb && go->ActivateToQuest(bot)))\n                    return false;" "quest herb without its quest is refused")
require_order("${loot_cpp}" "return hasQuestItems || go->GetLootState() != GO_READY;" "ai::cohort::Applies(sPlayerbotAIConfig.gatherQuestHerbFilter" "the active-quest branch stays first")
require_order("${loot_cpp}" "ai::cohort::Applies(sPlayerbotAIConfig.gatherQuestHerbFilter" "if (!ai->HasSkill((SkillType)skillId))" "before the herbalism skill check")

# Travel: #414 keeps quest herbs out of the gather targets in every mode (unchanged).
require_text("${travel_mgr}" "ai::gather_node::IsGatherNode(goInfo->type == GAMEOBJECT_TYPE_CHEST, lockHasNonProfessionSkill)" "#414 travel exclusion unchanged")
