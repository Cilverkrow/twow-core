# twow-repo#601 quest acceptance lever (iv) (OB-00 28.10.2026, cli-quest-organic/quest-acceptance/STEP2.md): the radius quest giver
# fetch of RequestQuestTravelTargetAction asked TravelMgr for entry 0, but AddDestination files every quest giver under its quest id,
# so it never returned a giver (givers came only from the whole-map fallback when nothing else came back). With
# AiPlayerbot.QuestFirstProgression.GiverFetchAnyQuest (0 off = default, 1 all, 2 cohort B) the giver fetch asks for every giver
# ({}) instead. This contract pins: the dist key, member and read (unknown = off), the parsed mode in [ConfigEcho] (sorted), the
# pure FetchEntries rule, the cohort bit read on the bot thread, its use in the fetch (giver fetch only), the filing rule it relies
# on, and the any_quest= field. The rule is tested in t/quest_search_policy_tests.cpp.

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
    message(FATAL_ERROR "#601 GiverFetchAnyQuest: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 GiverFetchAnyQuest: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("QuestSearchPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)

# 1. The switch: default 0 in the dist, the member and the read; unknown modes are off; parsed mode in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.QuestFirstProgression.GiverFetchAnyQuest = 0\n" "dist key, default 0")
require_text("${config_h}" "uint32 questFirstProgressionGiverFetchAnyQuest = 0;" "config member, default 0")
require_text("${config_cpp}" "questFirstProgressionGiverFetchAnyQuest = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.GiverFetchAnyQuest\", 0);\n    if (questFirstProgressionGiverFetchAnyQuest > 2)" "config read")
require_text("${config_cpp}" "        questFirstProgressionGiverFetchAnyQuest = 0;\n    }" "unknown mode = off")
require_text("${config_cpp}" "            {\"QuestFirstProgression.FailureClasses.GroupNeeded\", Value(questFirstProgressionFailureClassesGroupNeeded)},\n            {\"QuestFirstProgression.GiverFetchAnyQuest\", Value(questFirstProgressionGiverFetchAnyQuest)},\n            {\"QuestFirstProgression.GrindFallbackFailures\"" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. The pure rule: {} only for a giver fetch (quest id 0) with the mode, the quest id otherwise.
require_text("${policy}" "inline std::vector<int32_t> FetchEntries(bool giverFetchAnyQuest, int32_t questId)\n{\n    if (giverFetchAnyQuest && questId == 0)\n        return {};\n    return { questId };\n}" "FetchEntries")

# 3. What it relies on: quest relations are filed under their quest id (so entry 0 matches no giver).
require_text("${travel_h}" "uint32 id = questId ? questId : entry;" "AddDestination files a quest destination under its quest id")

# 4. The bit: set in the PlayerTravelInfo constructor on the bot thread - roster bot of the quest-first flow, no real player master,
#    the mode's cohort (review 28.10., as the sibling levers) - read by the job from its snapshot, used for the giver fetch only; the
#    giver fetch still carries quest id 0.
require_text("${travel_cpp}" "    giverAnyQuest = sPlayerbotAIConfig.questFirstProgressionEnabled && sRandomPlayerbotMgr.IsPersistentRosterMember(player->GetGUIDLow()) &&
        !ai->HasRealPlayerMaster() && ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionGiverFetchAnyQuest, player->GetGUIDLow());" "the gate in the constructor")
require_text("${choose}" "bool const giverAnyQuest = travelInfo.HasGiverAnyQuest();" "the job reads the snapshot bit")
require_text("${choose}" "purpose, ai::quest_search::FetchEntries(giverAnyQuest && (purpose & (uint32)TravelDestinationPurpose::QuestGiver), questId)," "giver fetch only")
require_text("${choose}" "std::vector<std::tuple<uint32, int32, float>> destinationFetches = { {(uint32)TravelDestinationPurpose::QuestGiver, 0," "the giver fetch carries quest id 0")
string(FIND "${choose}" "purpose, { questId }, true, range);" old_call)
if(NOT old_call EQUAL -1)
  message(FATAL_ERROR "#601 GiverFetchAnyQuest: the quest fetch still passes { questId } past the rule")
endif()

# 5. IsActive for a bot of the lever: the destination's own quest must be offered (filter F's rule), after filter F; the trace's
#    InactiveReasonOf mirrors it as no_mark.
require_text("${travel_cpp}" "        if (!forceThisQuest && info.HasGiverAnyQuest() &&\n            DialogStatusValue::getDialogStatus(bot, GetEntry(), GetQuestId()) != DIALOG_STATUS_AVAILABLE)\n            return false;" "own-quest rule in IsActive")
require_order("${travel_cpp}" "        if (!forceThisQuest && info.HasBandGiverFilter() &&" "        if (!forceThisQuest && info.HasGiverAnyQuest() &&" "after filter F")
require_order("${choose}" "                return Counts::InactiveMap;" "if (!info.HasFocusQuest() && info.HasGiverAnyQuest() &&" "mirrored in InactiveReasonOf")

# 6. Cost (review 28.10.): an every-giver fetch takes the range before IsPossible (same list); the whole-map fallback is skipped
#    when the any-quest radius fetch already reached its 10000 yd.
require_text("${travel_cpp}" "(onlyPossible && entries.empty() && (purposeFlag & (uint32)TravelDestinationPurpose::QuestGiver));" "range first for an every-giver fetch")
require_text("${choose}" "bool const giverFallback = list.empty() && anyGiverRange < 10000.0f;" "no second whole-map walk")
require_text("${choose}" "                if (giverAnyQuest && (purpose & (uint32)TravelDestinationPurpose::QuestGiver))\n                    anyGiverRange = std::max(anyGiverRange, range);" "the widest any-quest giver fetch")

# 7. The trace line says whether the rule applied.
require_text("${choose}" "giver_points=%u any_quest=%u\"," "any_quest= on the giver_fetch line")

message(STATUS "#601 GiverFetchAnyQuest source contract passed")
