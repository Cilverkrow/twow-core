cmake_policy(VERSION 3.16)
# twow-repo#601 owner rule R1 (owner 04.11.2026 via OB-00; CLI-Deep, ZoneKeepPolicy.h): AiPlayerbot.QuestFirstProgression.StaleKeepZone
# (cohort modes, default 0) - an empty quest search with stale skips neither widens nor counts toward a band step, and for .Seconds no
# quest point outside the bot's zone (turn-ins exempt); AiPlayerbot.QuestFirstProgression.StartZoneMinLevel (default 5 = the owner value since the SZM adoption 10.11.2026, 0 = rollback) -
# below it no travel point outside the start zone, no band step, no rescue out of it. Plus the lock-free stale query (cand4G T cause:
# one lock per candidate before). Pins the keys, the gates and their order, the counters and lines, the rescue gate and the fast path.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "zone keep: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "zone keep: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "zone keep: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "zone keep: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "zone keep: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "zone keep: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("ZoneKeepPolicy.h" policy)
read_source("QuestSearchPolicy.h" search_h)
read_source("TravelMgr.cpp" travel)
read_source("RandomPlayerbotMgr.cpp" random_mgr)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)

# 1. Keys: StaleKeepZone default 0, StartZoneMinLevel default 5 (SZM adoption 10.11.2026, 0 = rollback), an unknown value is off,
#    the keep time clamped, documented, echoed in sorted order.
require_text("${config_h}" "uint32 questFirstProgressionStaleKeepZone = 0;" "StaleKeepZone member off")
require_text("${config_h}" "uint32 questFirstProgressionStartZoneMinLevel = 5;" "StartZoneMinLevel member 5 (SZM adoption)")
require_text("${config_cpp}" "questFirstProgressionStaleKeepZone = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StaleKeepZone\", 0);" "StaleKeepZone default 0")
require_text("${config_cpp}" "if (questFirstProgressionStaleKeepZone > 3)" "unknown mode off")
require_text("${config_cpp}" "std::min<uint32>(3600, uint32(std::max<int32>(60, config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StaleKeepZone.Seconds\", 900))))" "keep time 60..3600, default 900")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StartZoneMinLevel\", 5)" "StartZoneMinLevel default 5")
require_text("${config_cpp}" "if (questFirstProgressionStartZoneMinLevel == 1 || questFirstProgressionStartZoneMinLevel > 60)" "StartZoneMinLevel 2..60")
require_order("${config_cpp}" "{\"QuestFirstProgression.SkipSkillLockedObjectives\"" "{\"QuestFirstProgression.StaleKeepZone\", Value(questFirstProgressionStaleKeepZone)}," "echo sorted (before)")
require_order("${config_cpp}" "{\"QuestFirstProgression.StaleKeepZone.Seconds\", Value(questFirstProgressionStaleKeepZoneSeconds)}," "{\"QuestFirstProgression.StaleObjectiveFilter\", Value(" "echo sorted (StaleK < StaleO)")
require_order("${config_cpp}" "{\"QuestFirstProgression.StaleObjectiveFilter.WindowSeconds\"" "{\"QuestFirstProgression.StartZoneMinLevel\", Value(questFirstProgressionStartZoneMinLevel)}," "echo sorted (Stal < Star)")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StaleKeepZone = 0\n" "documented off")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StaleKeepZone.Seconds = 900\n" "documented keep time")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StartZoneMinLevel = 5\n" "documented 5 (SZM adoption)")
require_text("${conf_dist}" "#   0 = off (rollback), 2..60, default 5 (the owner value); other values act like 0." "dist value text: 0 = rollback, 5 = default")
require_text("${config_h}" "uint32 questFirstProgressionStartZoneMinLevelCohort = 1;" "cohort selector default all")
require_text("${config_cpp}" "if (questFirstProgressionStartZoneMinLevelCohort > 3)" "unknown cohort selector acts like 1")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Cohort = 1\n" "documented cohort selector")
require_text("${choose}" "ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevelCohort, bot->GetGUIDLow());" "cohort selector in the travel gates")

# 2. The rule (pure): the start zone, the gates, the window across the wrap, turn-ins exempt.
require_text("${policy}" "#include \"StartZonePolicy.h\"" "custom start zones from StartZonePolicy")
require_text("${policy}" "return minLevel != 0 && level < minLevel;" "gated below the level only")
require_text("${policy}" "return gated && startKnown && targetZone != 0 && !targetInStart;" "fail open on unknown zones")
require_text("${policy}" "return applies && staleSkipped != 0;" "keeps only with stale skips")
require_text("${policy}" "return untilSec != 0 && std::int32_t(untilSec - nowSec) > 0;" "window, differences only")
require_text("${policy}" "return active && !turnIn && botZone != 0 && targetZone != 0 && targetZone != botZone;" "turn-ins exempt")
forbid_text("${policy}" "Player*" "a game type in the pure policy")

# 3. The deferrals: after LevelGuard, roster bots on their own; StartZone also for forced targets ("never below 5"), StaleKeepZone never
#    for forced ones; the area lookup only when the key applies; counted.
between("${choose}" "PostActiveDeferral PostActiveDeferralOf(" "\n}\n" deferral_of)
require_order("${deferral_of}" "return PostActiveDeferral::LevelGuard;" "return PostActiveDeferral::StartZone;" "after LevelGuard")
require_text("${deferral_of}" "if (position && sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevel && ai::zone_keep::StartZoneHolds(bot, ai) &&\n        ai::zone_keep::OutsideStartZone(bot, position))\n        return PostActiveDeferral::StartZone;" "start zone gate, forced targets too")
require_text("${deferral_of}" "if (!target->IsForced() && position && sPlayerbotAIConfig.questFirstProgressionStaleKeepZone &&\n        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&" "keep zone gate, never forced")
require_order("${deferral_of}" "ai::zone_keep::KeepActive(" "AreaTableEntry const* area = position->GetArea();" "area lookup only inside the window")
require_text("${deferral_of}" "ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStaleKeepZone, bot->GetGUIDLow())" "keep window cohort-gated")
require_text("${deferral_of}" "(uint32(destination->GetPurpose()) & uint32(TravelDestinationPurpose::QuestTaker)) != 0" "turn-ins exempt")
require_text("${choose}" "case PostActiveDeferral::StartZone:   // twow-repo#601 owner rule R1 (StartZoneMinLevel)\n                        ++lastRejects.startZone;\n" "start zone counted")
require_text("${choose}" "case PostActiveDeferral::StaleKeepZone:   // twow-repo#601 owner rule R1 (StaleKeepZone)\n                        ++lastRejects.staleKeepZone;\n                        continue;" "keep zone counted")
require_text("${search_h}" "(startZone ? \" start_zone=\" + std::to_string(startZone) : std::string())" "line only when counted")
require_text("${search_h}" "(staleKeepZone ? \" stale_keep_zone=\" + std::to_string(staleKeepZone) : std::string())" "line only when counted")

# 4. The empty search: the keep decision before the widening; no widening, no band streak or step while it keeps; no band step below
#    StartZoneMinLevel.
require_order("${choose}" "if (lastStaleObjective)\n                sLog.outBasic(\"[QuestFirstRoute] state=stale_objective" "bool const staleKeepZone = zoneRuleBot &&" "decided after the stale count")
require_text("${choose}" "            else\n                WidenQuestSearch(uint32(destinationList.size()));" "no widening while keeping")
require_text("${choose}" "SET_AI_VALUE2(int, \"manual int\", StaleKeepZoneUntilValue, int(until));" "window set")
require_text("${choose}" "[ZoneKeep] state=stale_keep bot=%u level=%u zone=%u skipped=%u seconds=%u" "visible keep")
require_text("${choose}" "if (sPlayerbotAIConfig.questFirstProgressionBandLadder && sPlayerbotAIConfig.BandStreakOn() && !staleKeepZone)" "no streak while keeping")
require_text("${choose}" "if (sPlayerbotAIConfig.questFirstProgressionBandLadder && !staleKeepZone && !startZoneHold &&" "no band step")

# 5. The rescue: below StartZoneMinLevel never out of the start zone - before every other rescue rule.
between("${random_mgr}" "std::vector<WorldLocation> RandomPlayerbotMgr::QuestRescueTargets(Player* bot)" "\n}\n" rescue_fn)
require_order("${rescue_fn}" "if (startZoneRules && ai::zone_keep::StartZoneGated(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevel, level) &&\n        ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevelCohort, bot->GetGUIDLow()))" "if (ai::quest_search::goblin_island::UsesGoblinStartRescue(" "gate first, cohort-selected")
require_text("${rescue_fn}" "[ZoneKeep] state=rescue_gate bot=%u level=%u zone=%u start_zone=%u in_start=%u result=%s" "visible gate")
# SZM adoption review (10.11.2026): roster bots without a real player master only (as StartZoneHolds); a custom start race without
# CustomRaceStartZones has an unknown start zone (proxy start vs the playercreateinfo zone) and fails open.
require_text("${rescue_fn}" "bool const startZoneRules = IsPersistentRosterMember(bot->GetGUIDLow()) && !(rescueAi && rescueAi->HasRealPlayerMaster());" "rescue gate scope = travel gate scope")
require_text("${policy}" "inline bool StartZoneKnown(std::uint32_t race, bool customStarts, std::uint32_t createZone)\n{\n    if (start_zone::IsCustomStartRace(race))\n        return customStarts;\n    return createZone != 0;\n}" "custom start races known only with custom starts")

# 6. The stale query: no lock while no mark is active; a per-thread snapshot rebuilt under the one mutex after a store change.
between("${travel}" "bool ai::stale_objective::IsObjectiveStale(" "\n}\n" query_fn)
require_order("${query_fn}" "if (!staleObjectiveAnyMark.load(std::memory_order_acquire) ||" "std::unique_lock<std::mutex> lock(staleObjectiveMutex, std::try_to_lock);" "fast path before the lock (try_lock first: [StaleObjectiveMin] counts the waits, StaleObjectiveFilter.LockFree pair)")
require_text("${query_fn}" "thread_local Snapshot snapshot;" "per-thread snapshot")
require_text("${query_fn}" "if (!snapshot.built || snapshot.version != version)" "rebuilt only after a change")
between("${travel}" "void ai::stale_objective::NoteObjectiveFailure(" "\n}\n" note_fn)
require_text("${note_fn}" "staleObjectiveStoreVersion.fetch_add(1, std::memory_order_acq_rel);" "a failure changes the version")
between("${travel}" "void ai::stale_objective::NoteQuestCredit(" "\n}\n" credit_fn)
require_text("${credit_fn}" "staleObjectiveStoreVersion.fetch_add(1, std::memory_order_acq_rel);" "a credit changes the version")

# 7. Forced trips below StartZoneMinLevel (OB-00 04.11.: "never below 5"): held, one line per trip and hold period, no retry every tick.
read_source("TravelMgr.h" travel_h)
read_source("QuestGroupWorld.cpp" group_world)
require_text("${travel_h}" "bool StartZoneHolds(Player* bot, PlayerbotAI* botAI);" "shared gate declared")
between("${travel}" "bool ai::zone_keep::StartZoneHolds(" "\n}\n" holds_fn)
require_text("${holds_fn}" "ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevelCohort, bot->GetGUIDLow())" "cohort-selected")
require_text("${holds_fn}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !(botAI && botAI->HasRealPlayerMaster())" "roster bots on their own")
between("${travel}" "void ai::zone_keep::NoteForcedHold(" "\n}\n" hold_fn)
require_text("${hold_fn}" "if (!bot || !botAI || ai::zone_keep::ForcedHoldActive(botAI))\n        return;" "one line per hold period")
require_order("${hold_fn}" "->Set(int(uint32(time(nullptr)) + StartZoneForcedHoldSeconds));" "[ZoneKeep] state=forced_hold bot=%u" "hold set with the line")
require_text("${travel}" "constexpr uint32 StartZoneForcedHoldSeconds = 600;" "hold period")
require_text("${choose}" "if (target->IsForced())\n                            ai::zone_keep::NoteForcedHold(bot, ai," "the main loop holds a deferred forced trip")
require_text("${choose}" "if ((travelName == \"guild meeting\" || travelName == \"guild order\" || travelName == \"petition\") &&\n        ai::zone_keep::StartZoneHolds(bot, ai) && ai::zone_keep::ForcedHoldActive(ai))\n        return false;" "no named forced request during the hold")
require_order("${group_world}" "if (startZoneHolds && ai::zone_keep::ForcedHoldActive(ai))\n        return false;" "TravelDestination* dest = FindQuestDest(bot, questId, objIdx, point);" "no group quest search during the hold")
require_order("${group_world}" "ai::zone_keep::NoteForcedHold(bot, ai, \"group_quest\", outsideZone);" "t->SetTarget(dest, point);" "the group quest trip held before it is set")

# 8. Owner question (04.11.): a held forced trip never turns into a trip without a valid target - the deferred point is skipped before
#    the target is set (the loop continues; an all-deferred choice sets no target and the bot keeps its normal quest choices), and a
#    named forced request during the hold returns before any destination search starts.
between("${choose}" "bool ChooseTravelTargetAction::SetBestTarget(" "\n}\n" best_fn)
require_order("${best_fn}" "case PostActiveDeferral::StartZone:" "target->SetTarget(destination, position);" "deferred before the target is set")
between("${choose}" "bool RequestNamedTravelTargetAction::Execute(" "\n}\n" named_fn)
require_order("${named_fn}" "ai::zone_keep::StartZoneHolds(bot, ai) && ai::zone_keep::ForcedHoldActive(ai))\n        return false;" "*AI_VALUE(FutureDestinations*, \"future travel destinations\") = std::async(" "no search during the hold")

# 9. OB-10 review #833: the zone deferrals are never "structural" (QuestFailureClassPolicy OtherRejects, so no park-all and no abandon)
#    and never a turn-in no-route verdict (unjudged in CountsAsNoRoute, like moved away / range skip).
read_source("QuestFailureClassPolicy.h" failure_h)
require_text("${failure_h}" "rejects.battlegroundQuest + rejects.startZone + rejects.staleKeepZone;" "zone deferrals in OtherRejects")
require_text("${choose}" "lastRejects.movedAway + lastRejects.rangeSkip + lastRejects.resumeSkipped + lastRejects.startZone + lastRejects.staleKeepZone," "zone deferrals unjudged for the turn-in park")
forbid_text("${choose}" "lastRejects.movedAway + lastRejects.rangeSkip + lastRejects.resumeSkipped,\n" "an unjudged sum without the zone deferrals")
