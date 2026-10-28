# twow-repo#601 quest acceptance step 1b (OB-00 27.10.2026, cli-quest-organic/quest-acceptance/STEP2.md): in AB-2 not one normal
# quest giver was routed to; uncontested giver fetches found no active giver in 90 % of the requests and every giver point failed
# IsActive as no_mark. Log only, behind the existing traces: (1) a traced giver fetch counts what
# QuestRelationTravelDestination::IsPossible drops, by rule, on its own PlayerTravelInfo copy, and writes one
# "[QuestFirstRoute] state=giver_fetch" line; TravelMgr::GetDestinations takes the range first for it only, so the counts cover
# the givers in reach and the list stays the same. (2) With DirectTargets.TraceInactive and TraceTravelDecisions the no_mark
# count is split by the first Player::CanTakeQuest check the point's own quest fails. The rules are tested in
# t/quest_search_policy_tests.cpp.

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
    message(FATAL_ERROR "#601 giver trace: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 giver trace: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text first last out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#601 giver trace: region not found: ${first}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${last}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#601 giver trace: region end not found after ${first}")
  endif()
  string(SUBSTRING "${tail}" 0 ${b} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  string(REPLACE "${needle}" "" stripped "${text}")
  string(LENGTH "${text}" full)
  string(LENGTH "${stripped}" rest)
  string(LENGTH "${needle}" len)
  math(EXPR n "(${full} - ${rest}) / ${len}")
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "#601 giver trace: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

read_source("QuestSearchPolicy.h" policy)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("aiplayerbot.conf.dist.in" dist)

# 1. Pure policy: the CanTakeQuest order of NoMarkOf, the drop rules and the line fields.
require_order("${policy}" "if (!c.maxLevel) return NoMarkMaxLevel;" "if (!c.status)" "max level before the status (CanTakeQuest)")
require_order("${policy}" "if (!c.status)" "if (!c.exclusive) return NoMarkExclusive;" "status before the exclusive group")
require_order("${policy}" "if (!c.level) return NoMarkLevel;" "if (!c.prevQuest) return NoMarkPrevQuest;" "level before the previous quest")
require_order("${policy}" "if (!c.prevQuest) return NoMarkPrevQuest;" "if (!c.chain) return NoMarkChain;" "previous quest before the chain")
require_text("${policy}" "return !c.notRewarded ? NoMarkDone : NoMarkInLog;" "a failed status is done or in the log")
require_text("${policy}" "return out + \" giver_kept=\" + std::to_string(kept) + \" giver_out_of_range=\" + std::to_string(outOfRange);" "giver_fetch fields")
require_text("${policy}" "        if (noMarkSum)\n        {\n            out += \" no_mark_why=\";" "no_mark_why only when counted")
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc MATCHES "^#include <")
    message(FATAL_ERROR "#601 giver trace: QuestSearchPolicy.h is not pure: ${inc}")
  endif()
endforeach()

# 2. The counter pointer: on PlayerTravelInfo, null by default.
require_text("${travel_h}" "ai::quest_search::GiverDropCounts* giverDrops = nullptr;" "null by default")
require_text("${travel_h}" "void SetGiverDrops(ai::quest_search::GiverDropCounts* counts) { giverDrops = counts; }" "setter")

# 3. IsPossible: givers only, every refusal of the giver branch counted with its rule, kept at the end of the giver branch.
region("${travel_cpp}" "bool QuestRelationTravelDestination::IsPossible(const PlayerTravelInfo& info) const" "\n}\n" possible)
require_text("${possible}" "ai::quest_search::GiverDropCounts* const giverDrops = GetRelation() == 0 ? info.GetGiverDrops() : nullptr;" "givers only")
require_text("${possible}" "if (!info.GetBoolValue2(\"has strategy\", \"rpg quest\"))\n        return refuse(ai::quest_search::GiverDropStrategy);" "strategy")
require_text("${possible}" "if (forceThisQuest && !info.IsFocusQuest(GetQuestId()))\n        return refuse(ai::quest_search::GiverDropFocus);" "focus")
require_text("${possible}" "(int32)info.GetLevel() + (int32)5)\n            return refuse(ai::quest_search::GiverDropQuestLevel);" "quest level")
require_text("${possible}" "(int32)info.GetLevel() > quest->GetMaxLevel())\n            return refuse(ai::quest_search::GiverDropMinMaxLevel);" "min / max level")
require_text("${possible}" "quest->GetRequiredClasses() != 0))\n            return refuse(ai::quest_search::GiverDropBandGrey);" "band grey")
require_text("${possible}" "if (quest->GetRequiredCondition())          //So we skip this quest for now.\n                return refuse(ai::quest_search::GiverDropCondition);" "condition")
count_text("${possible}" "return refuse(ai::quest_search::GiverDropFreeSlots);" 2 "both free slot thresholds")
require_text("${possible}" "!info.GetBoolValue(\"can fight boss\"))\n                return refuse(ai::quest_search::GiverDropElite);" "elite / dungeon")
require_order("${possible}" "return refuse(ai::quest_search::GiverDropElite);" "            ++giverDrops->kept;" "kept after the last giver rule")
require_order("${possible}" "            ++giverDrops->kept;" "    else\n    {" "kept inside the giver branch")
region("${possible}" "    if (GetRelation() == 0)" "    else\n    {" giver_branch)
string(FIND "${giver_branch}" "return false;" bare)
if(NOT bare EQUAL -1)
  message(FATAL_ERROR "#601 giver trace: an uncounted 'return false;' in the giver branch of IsPossible")
endif()

# 4. GetDestinations: the traced range first, in both loops, before IsPossible; untraced: nothing changes.
region("${travel_cpp}" "DestinationList TravelMgr::GetDestinations(" "\nvoid TravelMgr::GetPartitionsLock(" dest)
require_text("${dest}" "ai::quest_search::GiverDropCounts* const tracedGivers = onlyPossible ? info.GetGiverDrops() : nullptr;" "only a traced fetch")
require_text("${dest}" "        if (!rangeFirst)\n            return false;" "no traced and no every-giver fetch: no range check of its own (lever iv)")
require_text("${dest}" "            if (tracedGivers)\n                ++tracedGivers->outOfRange;" "out_of_range counted only when traced")
count_text("${dest}" "if (outOfTracedRange(dest))\n" 2 "the traced range check in both loops")
require_order("${dest}" "if (outOfTracedRange(dest))" "if (onlyPossible && !dest->IsPossible(info))" "range before IsPossible (gated path)")

# 5. The quest fetch job: a counting copy only with the trace (constructed only then), used for the giver fetches only - the
#    radius one and the whole-map fallback (review 27.10.: the radius one asks for entry 0 and returns no giver) - one line after
#    both; the untraced fetch order (loop, hub sort, fallback) unchanged.
require_text("${choose}" "bool const traceGivers = sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions && UsesQuestFirstProgression(bot);" "the request line's bots")
require_text("${choose}" "            std::optional<PlayerTravelInfo> giverInfo;\n            if (traceGivers)\n            {\n                giverInfo.emplace(travelInfo);\n                giverInfo->SetGiverDrops(&giverDrops);\n            }" "copy constructed only with the trace")
require_text("${choose}" "bool const tracedGiverFetch = traceGivers && (purpose & (uint32)TravelDestinationPurpose::QuestGiver);" "giver fetch only")
require_text("${choose}" "tracedGiverFetch ? *giverInfo : travelInfo," "the copy only for the traced giver fetch")
require_text("${choose}" "list = sTravelMgr.GetPartitions(center, partitions, traceGivers ? *giverInfo : travelInfo, (uint32)TravelDestinationPurpose::QuestGiver);" "the fallback counted when traced")
require_order("${choose}" "                SortQuestHubsFirst(list);" "bool const giverFallback = list.empty()" "hub sort before the fallback, as before")
require_text("${choose}" "sLog.outBasic(\"[QuestFirstRoute] state=giver_fetch bot=%u level=%u stage=%u radius=%.0f fetch=%s free_slots=%u \"" "the giver_fetch line")
require_order("${choose}" "bool const giverFallback = list.empty()" "if (traceGivers && (giverFetched || giverFallback))" "the line after the fallback")

# 6. no_mark split: inside the TraceInactive block, only for a no_mark, only with TraceTravelDecisions; core checks without messages.
require_text("${choose}" "if (reason == ai::quest_search::RejectCounts::InactiveNoMark && sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions)\n                        ++lastRejects.noMarkWhy[NoMarkWhyOf(bot, destination)];" "the split")
region("${choose}" "ai::quest_search::NoMarkReason NoMarkWhyOf(Player* bot, TravelDestination* destination)" "\n}\n" why)
foreach(check SatisfyQuestStatus SatisfyQuestExclusiveGroup SatisfyQuestClass SatisfyQuestRace SatisfyQuestLevel SatisfyQuestSkill
    SatisfyQuestCondition SatisfyQuestReputation SatisfyQuestPreviousQuest SatisfyQuestTimed SatisfyQuestNextChain SatisfyQuestPrevChain
    SatisfyQuestChallenges)
  require_text("${why}" "bot->${check}(quest, false)" "${check} without messages")
endforeach()
require_text("${why}" "ai::quest_search::NoMarkReason const why = ai::quest_search::NoMarkOf(checks);" "the pure order")
require_order("${why}" "if (why != ai::quest_search::NoMarkOther)" "switch (DialogStatusValue::getDialogStatus(bot, relation->GetEntry(), relation->GetQuestId()))" "a takeable quest split by its own dialog status")
require_text("${why}" "    case DIALOG_STATUS_AVAILABLE:\n        return ai::quest_search::NoMarkMasked;" "masked")
require_text("${why}" "    case DIALOG_STATUS_REWARD_REP:\n        return ai::quest_search::NoMarkRepeatable;" "repeatable")

# 7. Documented with the trace key.
require_text("${dist}" "\"[QuestFirstRoute] state=giver_fetch bot= level= stage= radius= fetch=radius|fallback free_slots= can_fight_equal=" "documented")

message(STATUS "#601 giver trace source contract passed")
