cmake_policy(VERSION 3.16)
# twow-repo#601 cand4 (i) (CLI-Deep 29.10.2026; OB-00 after CLI-PATH's as-1-why: in cand4r-B 568 of 716 cohort-B deaths fell on quests
# no bot made progress on all run): AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter (default 0) - a realm-wide memory of
# objective failures (work timeout, pos-stuck, emptied camp, death) of roster bots on their own; an objective with .Failures failures
# from .Bots distinct bots within .WindowSeconds and no credit of its quest since is skipped as a quest target for .Seconds; any kill
# or item credit of the quest clears it. Pins the keys, the rule, the bounded store and its short lock, the feeds, the cohort-gated
# skip (objectives only) and the counter line.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "stale objective: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "stale objective: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "stale objective: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "stale objective: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "stale objective: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "stale objective: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("StaleObjectivePolicy.h" policy)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/actions/ChooseTravelTargetAction.h" choose_h)
read_source("strategy/actions/QuestAction.cpp" quest_action)

# 1. Keys: neutral default, an unknown mode is off, parameters clamped, documented, echoed.
require_text("${config_h}" "uint32 questFirstProgressionStaleObjectiveFilter = 0;" "member default off")
require_text("${config_cpp}" "questFirstProgressionStaleObjectiveFilter = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter\", 0);" "default 0")
require_text("${config_cpp}" "if (questFirstProgressionStaleObjectiveFilter > 3)" "unknown mode checked (3 = cohort A, the crossover arm)")
require_text("${config_cpp}" "std::min<uint32>(8, uint32(std::max<int32>(1, config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.Failures\", 4))))" "failures 1..8, default 4")
require_text("${config_cpp}" "std::min<uint32>(8, uint32(std::max<int32>(1, config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.Bots\", 3))))" "bots 1..8, default 3")
require_text("${config_cpp}" "{\"QuestFirstProgression.StaleObjectiveFilter\", Value(questFirstProgressionStaleObjectiveFilter)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StaleObjectiveFilter = 0\n" "documented off")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.Failures = 4\n" "documented failures")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.Bots = 3\n" "documented bots")

# 2. The rule (pure): window, distinct bots, a new mark only, a credit drops the quest's keys, bounded.
require_text("${policy}" "if (IsMarked(entry, nowMs) || failuresOut < params.failures || botsOut < params.bots)\n        return false;" "marks on failures + distinct bots, never renews")
require_text("${policy}" "auto it = store.entries.lower_bound(Key{ questId, INT32_MIN });" "a credit drops its quest's keys")
require_text("${policy}" "constexpr std::size_t MaxKeys = 2048;" "bounded store")
require_text("${policy}" "if (store.entries.find(key) == store.entries.end())\n        MakeRoom(store, nowMs, params);" "room made before a new key")
forbid_text("${policy}" "#include \"" "a game header in the pure policy")

# 3. The store: one mutex, the map operation inside the lock, the log line after it; the switch off returns at once.
require_text("${travel}" "std::mutex staleObjectiveMutex;" "one mutex")
between("${travel}" "void ai::stale_objective::NoteObjectiveFailure(" "\n}\n" note_fn)
require_text("${note_fn}" "if (!sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilter || !bot || !objective)\n        return;" "off: nothing recorded")
require_text("${note_fn}" "if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) || (botAI && botAI->HasRealPlayerMaster()) ||\n        bot->GetMap()->IsDungeon() || bot->InBattleGround())" "roster bots on their own, open world")
require_order("${note_fn}" "marked = ai::stale_objective::RecordFailure(" "    if (marked)\n        sLog.outBasic(\"[StaleObjective] state=mark" "the line after the lock")
between("${travel}" "void ai::stale_objective::NoteQuestCredit(" "\n}\n" credit_fn)
require_text("${credit_fn}" "ended = ai::stale_objective::Credit(staleObjectiveStore, questId, WorldTimer::getMSTime());" "credit clears")
require_text("${credit_fn}" "[StaleObjective] state=clear" "visible clear")
require_text("${travel_h}" "bool IsObjectiveStale(QuestObjectiveTravelDestination const* objective);" "query declared")

# 4. The feeds: every objective failure (all three QL-1 sites go through NoteQuestObjectiveFailure) and a death at an objective; the
#    kill and item credits.
between("${travel}" "void TravelTarget::NoteQuestObjectiveFailure()" "\n}\n" ql1_fn)
require_order("${ql1_fn}" "ai::stale_objective::NoteObjectiveFailure(bot, ai, objective, \"failure\");" "if (!sPlayerbotAIConfig.questLoopMaxFailures || !objective" "fed before QL-1's own switch")
between("${travel}" "void TravelTarget::OnDeathAtDestination()" "\n}\n" death_fn)
require_text("${death_fn}" "ai::stale_objective::NoteObjectiveFailure(bot, ai, objective, \"death\");" "a death at an objective")
between("${quest_action}" "bool QuestUpdateAddKillAction::Execute(Event& event)" "\n}\n" kill_fn)
require_text("${kill_fn}" "ai::stale_objective::NoteQuestCredit(bot, questId);" "kill credit")
between("${quest_action}" "bool QuestUpdateAddItemAction::Execute(Event& event)" "\n}\n" item_fn)
require_text("${item_fn}" "ai::stale_objective::NoteQuestCredit(bot, pair.first->GetQuestId());" "item credit")

# 5. The skip: CandidateSkipOf, after SkillLocked, switch + cohort, roster bot on its own, objectives only; counted, own line when > 0.
between("${choose}" "CandidateSkip CandidateSkipOf(" "\n}\n" skip_of)
require_text("${skip_of}" "if (ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilter, bot->GetGUIDLow()) &&\n        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster())\n        if (QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(destination))\n            if (ai::stale_objective::IsObjectiveStale(objective))\n                return CandidateSkip::StaleObjective;" "gate, objectives only")
require_order("${skip_of}" "return CandidateSkip::SkillLocked;" "return CandidateSkip::StaleObjective;" "after SkillLocked")
require_text("${choose}" "case CandidateSkip::StaleObjective:\n                    ai->TellDebug(requester, \"Skipping quest objective that many bots failed at without credit.\", \"debug travel\");\n                    ++lastStaleObjective;\n                    continue;" "counted and skipped")
require_text("${choose}" "lastStaleObjective = 0;" "reset per choice")
require_text("${choose_h}" "uint32 lastStaleObjective = 0;" "counter member")
require_text("${choose}" "if (lastStaleObjective)\n                sLog.outBasic(\"[QuestFirstRoute] state=stale_objective bot=%u level=%u skipped=%u\", bot->GetGUIDLow(), bot->GetLevel(), lastStaleObjective);" "own line only when counted")
