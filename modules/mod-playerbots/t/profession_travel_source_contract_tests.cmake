# twow-repo#333 Berufe P1-a / P1-b (CLI-BERUFE PLAN-B1, OB-00 go 25.10.2026): AiPlayerbot.ProfessionTraining.Travel and
# AiPlayerbot.ProfessionUse.ToolBuy, modes 0 / 1 / 2 (in-realm A/B), default 0, ConfigEcho. Gap-filler trips of a roster bot
# to the trainer of a phase-one profession and to the vendor of its missing pick / knife, below the quest request (6.3);
# with ToolBuy a mining/skinning gather purpose needs its tool. #306 stays: "trainer trade" is still refused for roster bots.

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
    message(FATAL_ERROR "#333 ProfessionTravel: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#333 ProfessionTravel: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 ProfessionTravel: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("ProfessionTravelPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/values/TravelValues.cpp" values_cpp)
read_source("strategy/values/TravelValues.h" values_h)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("strategy/generic/TravelStrategy.cpp" strategy_cpp)
read_source("PersistentRosterProfessionTrainingPolicy.h" training_policy)

# Switches: modes, default 0, unknown -> 0, documented, echoed.
foreach(pair "professionTrainingTravel|ProfessionTraining.Travel" "professionUseToolBuy|ProfessionUse.ToolBuy")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 member)
  list(GET parts 1 key)
  require_text("${config_h}" "uint32 ${member} = 0;" "${key} default off, a mode")
  require_text("${config_cpp}" "${member} = config.GetIntDefault(\"AiPlayerbot.${key}\", 0);" "${key} config read")
  require_text("${config_cpp}" "if (${member} > 2)\n        ${member} = 0;" "${key} unknown mode is off")
  require_text("${config_cpp}" "{\"${key}\", Value(${member})}," "${key} ConfigEcho entry")
  require_text("${conf_dist}" "\nAiPlayerbot.${key} = 0\n" "${key} documented, 0 by default")
  forbid_text("${conf_dist}" "\nAiPlayerbot.${key} = 1" "${key} default 1 in the dist")
  forbid_text("${conf_dist}" "\nAiPlayerbot.${key} = 2" "${key} default 2 in the dist")
endforeach()

# Pure rules: no game headers, the gap rule of CLI-Deep, phase-one skills, tools.
forbid_text("${policy}" "#include \"playerbot/" "game headers in the pure policy")
require_text("${policy}" "return now < backoffUntil && !turnInWaiting && bandTripEnd == 0;" "the quest-first gap")
require_text("${policy}" "return skill == kHerbalism || skill == kMining || skill == kSkinning || skill == kFirstAid;" "phase-one skills")
require_text("${policy}" "constexpr std::uint32_t kMiningPick = 2901;" "mining pick id")
require_text("${policy}" "constexpr std::uint32_t kSkinningKnife = 7005;" "skinning knife id")
require_text("${policy}" "constexpr std::uint32_t kMinLevel = 5;" "apprentice level")

# #306 unchanged: no remote "trainer trade" for roster bots.
require_text("${training_policy}" "return !(persistentRosterMember && tradeTrainer);" "#306 remote trainer refusal unchanged")

# Values: the cohort first, the gap values only read, the turn-in rule of 8.5, the hold names, before the generic trainer branch.
require_text("${values_cpp}" "bool const applies = ai::cohort::Applies(mode, bot->GetGUIDLow());" "mode and cohort")
require_text("${values_cpp}" "AI_VALUE2(int, \"manual int\", \"quest route backoff until\")" "the backoff is read")
forbid_text("${values_cpp}" "SET_AI_VALUE2(int, \"manual int\", \"quest route backoff until\"" "the backoff is never written here")
require_text("${values_cpp}" "return ai::profession_travel::InQuestGap(now, backoffUntil, ProfessionTripTurnInWaiting(bot, travelTarget),\n        travelTarget->GetBandTripEnd());" "an open turn-in and a band walk come first")
require_text("${values_cpp}" "    return !ProfessionTripTurnInWaiting(bot, AI_VALUE(TravelTarget*, \"travel target\"));" "a started trip ends when a turn-in opens")
require_text("${values_cpp}" "if (!sSpellMgr.IsProfessionSpell(trainerSpell->learnedSpell))" "ranks only, no recipe trips")
forbid_text("${conf_dist}" "[ProfTravel] state=start" "the documented line is state=request")
require_order("${values_cpp}" "else if (name == \"trainer profession\" || name == \"trainer profession hold\")" "else if (name.find(\"trainer\") == 0)" "the profession trip before the generic trainer branch")
require_text("${values_cpp}" "if (!ai::profession_travel::IsPhaseOneSkill(skill))" "only phase-one trainers")
require_text("${values_cpp}" "else if (name == \"tool vendor\" || name == \"tool vendor hold\")" "tool vendor value")
require_text("${values_h}" "std::vector<int32> PhaseOneProfessionTrainers(PlayerbotAI* ai);" "shared trainer list")
require_text("${values_h}" "uint32 MissingGatherTool(PlayerbotAI* ai);" "shared tool check")
# P1-b: the gather purpose needs its tool, after the 8.5 turn-in yield, before the purpose is declared.
require_order("${values_cpp}" "if (ai::quest_search::GatherYieldsToTurnIn(rosterOnItsOwn, finished))" "if (ai::cohort::Applies(sPlayerbotAIConfig.professionUseToolBuy, bot->GetGUIDLow()) &&" "tool check after the turn-in yield")
require_order("${values_cpp}" "!ai::profession_travel::HasGatherTool(gatheringSkills.at(purpose)," "switch (ai::gather_purpose::Decide(declared, rosterOnItsOwn, skill, value, target, now))" "tool check before the purpose is declared")

# Request: before the generic trainer branch, request time set before the empty check, one line, hold conditions, allowed.
require_order("${choose_cpp}" "else if (travelName == \"trainer profession\" || travelName == \"tool vendor\")" "else if (travelName.find(\"trainer\") == 0)" "the profession request before the generic trainer branch")
require_order("${choose_cpp}" "trainerTrip ? ai::profession_travel::kTrainerTripValue : ai::profession_travel::kToolTripValue," "        if (entries.empty())\n            return false;\n\n        TravelDestinationPurpose const purpose" "the request time counts an empty request too")
require_text("${choose_cpp}" "sLog.outBasic(\"[ProfTravel] state=request kind=%s bot=%u level=%u cohort=%c tool=%u entries=%u\"," "one line per request")
require_text("${choose_cpp}" "travelName == \"trainer profession\" ? \"should travel named::trainer profession hold\" :" "trainer trip hold condition")
require_text("${choose_cpp}" "travelName == \"tool vendor\" ? \"should travel named::tool vendor hold\" :" "tool trip hold condition")
require_text("${choose_cpp}" "else if (name == \"trainer profession\" || name == \"tool vendor\")   // twow-repo#333 Berufe P1-a / P1-b\n        return true;" "isAllowed")

# Strategy: rows only with the switch, below the quest request 6.3, above Explore 6.29.
require_text("${strategy_cpp}" "if (sPlayerbotAIConfig.professionUseToolBuy)\n        triggers.push_back(new TriggerNode(\n            travel_request::TriggerName(\"val::should travel named::tool vendor\", gateRequests),\n            NextAction::array(0, new NextAction(\"request named travel target::tool vendor\", 6.297f), NULL)));" "tool vendor row")
require_text("${strategy_cpp}" "if (sPlayerbotAIConfig.professionTrainingTravel)\n        triggers.push_back(new TriggerNode(\n            travel_request::TriggerName(\"val::should travel named::trainer profession\", gateRequests),\n            NextAction::array(0, new NextAction(\"request named travel target::trainer profession\", 6.296f), NULL)));" "trainer row")
require_text("${strategy_cpp}" "{\"val::has strategy::rpg quest\", \"request quest travel target\", 6.3f}" "quest request stays at 6.3")
require_text("${strategy_cpp}" "{\"\",TravelDestinationPurpose::Explore, 6.29f}," "Explore stays at 6.29")
