cmake_policy(VERSION 3.16)
# twow-repo#601 cr2-C GO diagnosis (CLI-Deep 26.10.2026, OB-00 go, rules agreed with CLI-Berufe): AiPlayerbot.QuestFirstProgression.
# SkipSkillLockedObjectives (default 0) - a game object quest objective whose lock opens only with profession skills the bot lacks
# (skill, value >= max(1, lock value), tool) is skipped by the candidate filter; a slot without a profession (open, kneeling, key
# item) keeps it a target for every bot. Pins the key, the any-slot rule, the slot mapping, the gate and the counter line (the rejected line stays unchanged).

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "skip skill locked: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "skip skill locked: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "skip skill locked: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "skip skill locked: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "skip skill locked: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("QuestSkillLockPolicy.h" policy)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/actions/ChooseTravelTargetAction.h" choose_h)

# 1. Key: neutral default, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 questFirstProgressionSkipSkillLockedObjectives = 0;" "member default off")
require_text("${config_cpp}" "questFirstProgressionSkipSkillLockedObjectives = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.SkipSkillLockedObjectives\", 0);" "default 0")
require_text("${config_cpp}" "if (questFirstProgressionSkipSkillLockedObjectives > 2)" "unknown mode checked")
require_text("${config_cpp}" "{\"QuestFirstProgression.SkipSkillLockedObjectives\", Value(questFirstProgressionSkipSkillLockedObjectives)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.SkipSkillLockedObjectives = 0\n" "documented")

# 2. The rule: any slot fits; a profession slot needs skill, value >= max(1, lock value) and the tool.
require_text("${policy}" "if (!slots[i].profession || slots[i].met)\n                return true;\n        return count == 0;" "any slot opens; no slot = open")
require_text("${policy}" "return hasSkill && hasTool && value >= std::max<std::uint32_t>(1, required);" "skill, value, tool")

# 3. The slot mapping: game object objectives only; key item and non-profession skill slots openable; tool via HasGatherTool.
between("${choose}" "bool SkillLockedObjective(Player* bot, TravelDestination const* destination)" "\n}\n" mapping)
require_text("${mapping}" "if (!objective || objective->GetEntry() >= 0)\n        return false;" "game object objectives only")
require_text("${mapping}" "if (lock->Type[j] == LOCK_KEY_ITEM)\n            slots[count++] = ai::quest_skill_lock::Slot{ false, true };" "key item slot openable")
require_text("${mapping}" "if (skill == SKILL_NONE)\n                slots[count++] = ai::quest_skill_lock::Slot{ false, true };" "open / kneeling slot openable")
require_text("${mapping}" "ai::profession_travel::HasGatherTool(skill, hasPick, hasKnife)" "tool (Mining: the pick)")
require_text("${mapping}" "return !ai::quest_skill_lock::Openable(slots, count);" "skipped only when not openable")

# 4. The gate in the candidate filter: last check, switch + cohort, roster bot on its own; counted, an own line only when > 0.
between("${choose}" "CandidateSkip CandidateSkipOf(" "\n}\n" skip_of)
require_text("${skip_of}" "if (ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionSkipSkillLockedObjectives, bot->GetGUIDLow()) &&\n        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() && SkillLockedObjective(bot, destination))\n        return CandidateSkip::SkillLocked;" "gate")
require_order("${skip_of}" "return CandidateSkip::GatherPurposeSuppressed;" "return CandidateSkip::SkillLocked;" "after the existing skips")
require_text("${choose}" "case CandidateSkip::SkillLocked:\n                    ai->TellDebug(requester, \"Skipping quest object that needs a profession skill I lack.\", \"debug travel\");\n                    ++lastSkillLocked;\n                    continue;" "counted and skipped")
require_text("${choose}" "lastSkillLocked = 0;" "reset per choice")
require_text("${choose_h}" "uint32 lastSkillLocked = 0;" "counter member")
require_order("${choose}" "\"no_active_route_candidate\", lastRejects.Format() + ai::contested_objective::FormatSources(lastSuppressSources));" "if (lastSkillLocked)\n                sLog.outBasic(\"[QuestFirstRoute] state=skill_locked bot=%u level=%u skipped=%u\", bot->GetGUIDLow(), bot->GetLevel(), lastSkillLocked);" "own line after the unchanged rejected line, only when counted")
