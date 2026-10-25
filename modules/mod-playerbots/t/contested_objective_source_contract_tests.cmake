# twow-repo#601 F5 (CLI-Deep 25.10.2026, OB-00 go): the death_suppressed skips by suppression source (diagnostic, log-only) and
# AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait (F5b, default 0): a work timeout at a contested quest objective is held
# back briefly and counts neither toward QuestWorkTimeouts (#405) nor toward QuestLoop (QL-1).

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
    message(FATAL_ERROR "F5 contested objective: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "F5 contested objective: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "F5 contested objective: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "F5 contested objective: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "F5 contested objective: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)
read_source("TravelMgr.h" travel_h)
read_source("DestinationDeathPolicy.h" death)
read_source("ContestedObjectivePolicy.h" policy)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/actions/ChooseTravelTargetAction.h" choose_h)
read_source("strategy/actions/ZoneEscapeActions.cpp" escape)

# 1. Keys: members, config reads with neutral defaults, documented in conf.dist; an unknown mode is off.
require_text("${config_h}" "uint32 questFirstProgressionContestedObjectiveWait = 0;" "mode member default off")
require_text("${config_h}" "bool questFirstProgressionContestedObjectiveWaitKill = false;" "Kill member default off")
require_text("${config_cpp}" "questFirstProgressionContestedObjectiveWait = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait\", 0);" "mode default 0")
require_text("${config_cpp}" "if (questFirstProgressionContestedObjectiveWait > 2)" "unknown mode checked")
require_text("${config_cpp}" "questFirstProgressionContestedObjectiveWaitKill = config.GetBoolDefault(\"AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait.Kill\", false);" "Kill default 0")
require_text("${config_cpp}" "\"AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait.MinBots\", 2)" "MinBots default 2")
require_text("${config_cpp}" "\"AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait.Seconds\", 300)" "Seconds default 300")
foreach(key "ContestedObjectiveWait = 0" "ContestedObjectiveWait.Kill = 0" "ContestedObjectiveWait.MinBots = 2" "ContestedObjectiveWait.Seconds = 300")
  require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.${key}\n" "documented ${key}")
endforeach()

# 2. F5b hook: switch first, before the QL-1 count and the #405 count of OnWorkTimeout; the turn-in branch stays first.
between("${travel}" "void TravelTarget::OnWorkTimeout()" "\n}\n" work)
require_text("${work}" "    if (sPlayerbotAIConfig.questFirstProgressionContestedObjectiveWait && OnContestedObjectiveTimeout())\n        return;" "switch first, then the contested check")
require_order("${work}" "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"work_timeout\");\n        return;" "OnContestedObjectiveTimeout()" "turn-ins keep their own path")
require_order("${work}" "OnContestedObjectiveTimeout()" "NoteQuestObjectiveFailure();" "before the QL-1 count")
require_order("${work}" "OnContestedObjectiveTimeout()" "destination_death::RecordDeath(workTimeouts[tDestination]" "before the #405 count")

# 3. The contested check: roster bot on its own, cohort mode, GO / kill rule with the bot-only group exception, bounded neighbour count
#    (sight list, positions only), a short hold instead of the hour, tagged and logged.
between("${travel}" "bool TravelTarget::OnContestedObjectiveTimeout()" "\n}\n" contested)
require_text("${contested}" "ai->HasRealPlayerMaster() || !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())" "roster bots on their own only")
require_text("${contested}" "ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionContestedObjectiveWait, bot->GetGUIDLow())" "cohort mode")
require_text("${contested}" "facts.goObjective = objective->GetEntry() < 0;" "GO objectives by entry sign")
require_text("${contested}" "facts.inBotOnlyGroup = group && ai::adhoc_group::AllRosterBots(group);" "bot-only group (CLI-GRP 25.10.)")
require_text("${contested}" "AI_VALUE(std::list<ObjectGuid>, \"nearest friendly players\")" "neighbours from the sight list")
require_text("${contested}" "bot->GetDistance2d(other) <= contested_objective::Radius" "within the radius")
forbid_text("${contested}" "\"travel target\"" "another bot's travel target read")
require_order("${contested}" "contested_objective::Contested(facts)" "SuppressCurrentDestination(holdSeconds * IN_MILLISECONDS);" "decide, then hold back")
require_order("${contested}" "SuppressCurrentDestination(holdSeconds * IN_MILLISECONDS);" "TagCurrentSuppression(destination_death::SourceContested);" "tagged contested")
require_text("${contested}" "\"[ContestedObjective] bot=%u level=%u quest=%u target_entry=%d kind=%s bots_near=%u min_bots=%u hold_seconds=%u\"" "greppable line")
forbid_text("${contested}" "NoteQuestObjectiveFailure" "a contested timeout counted for QL-1")
forbid_text("${contested}" "workTimeouts" "a contested timeout counted for #405")
require_text("${policy}" "if (!f.goObjective && (!f.killToo || f.inBotOnlyGroup))\n        return false;" "kill objectives only with Kill, outside bot-only groups")
require_text("${policy}" "constexpr float Radius = 40.0f;" "radius")

# 4. Diagnostic: every non-death writer of the shared table tags its source; the record tags deaths; the rejected line gets the
#    subsets while the death_suppressed total and RejectCounts stay as they are.
require_text("${death}" "enum Source : std::uint8_t { SourceDeath = 0, SourceWork = 1, SourceEscape = 2, SourcePark = 3, SourceContested = 4 };" "sources")
require_text("${death}" "    record.suppressUntil = now + cooldownMs;\n    record.source = SourceDeath;" "a death suppression is a death")
require_text("${travel}" "    SuppressCurrentDestination(cooldownMs);\n    TagCurrentSuppression(destination_death::SourceWork);" "work timeouts tagged")
require_text("${travel}" "    SuppressCurrentDestination(parkSeconds * IN_MILLISECONDS);\n    TagCurrentSuppression(destination_death::SourcePark);" "PosStuck parks tagged")
require_text("${escape}" "target->TagCurrentSuppression(destination_death::SourceEscape);" "zone escape tagged")
require_text("${travel_h}" "void TagCurrentSuppression(uint8 source);" "tag declared")
require_text("${choose_h}" "ai::contested_objective::SourceCounts lastSuppressSources{};" "subset counters")
require_text("${choose}" "lastRejects.Format() + ai::contested_objective::FormatSources(lastSuppressSources)" "subsets appended to the rejected line")
require_order("${choose}" "    lastRejects = ai::quest_search::RejectCounts();\n" "    lastSuppressSources = ai::contested_objective::SourceCounts{};\n" "reset with the reject counts")
require_text("${choose}" "                    ++lastRejects.deathSuppressed;\n                    if (persistentTarget)" "death skip counted as before, then its source")
require_text("${choose}" "                    ++lastRejects.deathSuppressed;\n                    ++lastSuppressSources[ai::contested_objective::SourceGather];" "gather purposes as their own source")

message(STATUS "contested_objective source contract passed")
