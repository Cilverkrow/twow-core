# twow-repo#587 (ZONE-01, owner 14.10.2026): AiPlayerbot.ZoneExit.* (all default 0). Quest rescue on the right continent,
# stranded bots ask for it, no battleground quests as quest-first targets.

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
    message(FATAL_ERROR "#587 zone exit: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#587 zone exit: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#587 zone exit: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#587 zone exit: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("PlayerbotAI.h" ai_h)

require_text("${config_h}" "bool zoneExitRescueHomeContinent = false;" "Z1a default off")
require_text("${config_h}" "uint32 zoneExitStrandedRescueAfter = 0;" "Z1b default off")
require_text("${config_h}" "bool zoneExitSkipBattlegroundQuests = false;" "Z3 default off")
require_text("${conf_dist}" "AiPlayerbot.ZoneExit.RescueHomeContinent = 0" "Z1a documented")
require_text("${conf_dist}" "AiPlayerbot.ZoneExit.StrandedRescueAfter = 0" "Z1b documented")
require_text("${conf_dist}" "AiPlayerbot.ZoneExit.SkipBattlegroundQuests = 0" "Z3 documented")

# Z1a: the rescue targets of one continent only; off home the bot's own race (the way home) counts.
between("${mgr}" "std::vector<WorldLocation> RandomPlayerbotMgr::QuestRescueTargets(Player* bot)" "\n}\n" rescue)
require_text("${rescue}" "bool const homeContinent = sPlayerbotAIConfig.zoneExitRescueHomeContinent && own" "behind the switch")
require_text("${rescue}" "ai::zone_exit::RescueContinent(bot->GetMapId(), own->mapId)" "home continent from the own race")
require_text("${rescue}" "(race == bot->getRace() && !offHome)" "own race only on the way home")
require_order("${rescue}" "if (homeContinent)" "[rescueMap](WorldLocation const& t) { return t.mapid != rescueMap; }" "other continent filtered out")
# The cross-continent quest route rule stays untouched (owner decision 04.10, #544).
string(FIND "${rescue}" "MinLevelForCrossMapQuestRoute" route_rule)
if (NOT route_rule EQUAL -1)
  message(FATAL_ERROR "#587 zone exit: the rescue must not change the cross-continent route rule")
endif()

# Z1b: counted in the empty quest search, asks for the rescue, the per-bot limit stays.
between("${choose}" "WidenQuestSearch(uint32(destinationList.size()));" "// Hotfix 8.1: wait before the next quest route request" stranded)
require_order("${stranded}" "if (sPlayerbotAIConfig.zoneExitStrandedRescueAfter && UsesQuestFirstProgression(bot))" "ai->RequestQuestRescue(uint32(time(nullptr)));" "behind the switch")
require_text("${stranded}" "ai::zone_exit::OnlyOtherContinent(lastRejects.crossMap, otherRejects)" "only other-continent candidates")
require_text("${ai_h}" "if (!lastQuestRescue.load() || now - lastQuestRescue.load() >= ai::quest_search::RescuePerBotSeconds)" "per-bot rescue limit")

# Z3: battleground quests skipped as targets, counted.
require_order("${choose}" "if (sPlayerbotAIConfig.zoneExitSkipBattlegroundQuests && !target->IsForced())" "++lastRejects.battlegroundQuest;" "BG skip behind the switch")
require_text("${choose}" "ai::zone_exit::IsBattlegroundQuestSort(bgQuestTemplate->GetZoneOrSort())" "by quest sort")

message(STATUS "zone_exit source contract passed")
