# twow-repo#541 (audit A12, cards 11/24): TrainableSpellsValue::Calculate ran Player::GetTrainerSpellState
# for every trainer spell of the bucket (trade: every recipe of every profession) and re-read the roster
# flag and profession pair (global eventCache, string keys) for every GREEN spell. With
# AiPlayerbot.Perf.TrainableSpellsPrecheck = 1, a spell that fails the core's own skill check
# (reqSkill && GetSkillValueBase(reqSkill) < reqSkillValue -> RED; all earlier exits RED/GRAY) is skipped
# before the state walk, and the roster state is read once, lazily at the first GREEN spell (in-memory
# lookups only: both paths run at most one eventCache DB load per bot). Same GREEN set, same order.
# Default 0 = old path. Pure rules: t/trainable_spells_precheck_policy_tests.cpp.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A12: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A12: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A12: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A12: ${description}: '${needle}' found ${count}x, expected ${expected}x")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A12: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A12: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("strategy/values/TrainerValues.cpp" tv)
read_source("TrainableSpellsPrecheckPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
region("${tv}" "std::vector<TrainerSpell const*> TrainableSpellsValue::Calculate()" "std::string TrainableSpellsValue::Format()" calc)

# 1. Pure policy: std only, the core condition verbatim, no level shortcut, no state.
require_text("${policy}" "namespace ai::trainable_spells" "policy namespace")
forbid_text("${policy}" "#include \"" "engine include in the pure policy")
require_text("${policy}" "return reqSkill && skillBase(reqSkill) < reqSkillValue;" "core RED skill condition (Player.cpp GetTrainerSpellState)")
require_text("${policy}" "return !reuse || !alreadyRead;" "lazy roster read rule")
forbid_text("${policy}" "static " "state in the policy")
forbid_text("${policy}" "reqLevel" "level pre-check")
require_text("${tv}" "#include \"playerbot/TrainableSpellsPrecheckPolicy.h\"" "policy include")

# 2. Switch read once per call, only in this function.
require_text("${calc}" "bool const trainablePrecheck = sPlayerbotAIConfig.perfTrainableSpellsPrecheck;" "switch read once per call")
require_count("${tv}" "perfTrainableSpellsPrecheck" 1 "switch read only in TrainableSpellsValue::Calculate")
require_order("${calc}" "bool rosterStateRead = false;" "for (auto& [trainerType, spellReqList] : *spellMap)" "call-local roster state before the loops")
forbid_text("${calc}" "static " "static cache")
forbid_text("${calc}" "thread_local" "thread-local cache")
forbid_text("${calc}" "shared_mutex" "shared_mutex")

# 3. Pre-check: gated, exact accessor and fields, directly followed by continue, before the (kept) state walk.
set(pre "if (trainablePrecheck && ai::trainable_spells::SkillRequirementRed(trainerSpell->reqSkill, trainerSpell->reqSkillValue, [this](uint32 skill) { return bot->GetSkillValueBase(skill); }))")
require_count("${calc}" "${pre}" 1 "gated exact pre-check")
require_text("${calc}" "${pre}\n                    continue;\n" "pre-check followed directly by continue")
require_count("${calc}" "SkillRequirementRed(" 1 "one pre-check site")
require_order("${calc}" "for (auto& [trainerSpell, trainers] : trainerSpellList)" "${pre}" "pre-check inside the spell loop")
require_order("${calc}" "${pre}" "TrainerSpellState state = bot->GetTrainerSpellState(trainerSpell, reqLevel);" "pre-check before the state walk")
require_count("${calc}" "TrainerSpellState state = bot->GetTrainerSpellState(trainerSpell, reqLevel);" 1 "state walk kept")
require_count("${calc}" "if (state != TRAINER_SPELL_GREEN)" 1 "only GREEN accepted")
forbid_text("${calc}" "GetSkillValue(" "non-base skill accessor")
forbid_text("${calc}" "GetSkillValuePure(" "pure skill accessor")
forbid_text("${calc}" "HasSkill(" "per-bucket skill skip")
forbid_text("${calc}" "> bot->GetLevel()" "level pre-check")
forbid_text("${calc}" "bot->GetLevel() < trainerSpell" "level pre-check")
require_count("${calc}" "requirement != bot->" 2 "only the class/race bucket filters (no TRADESKILLS bucket skip)")

# 4. Lazy roster read: after the GREEN check, gated by the policy, one read site in the body.
#    Off = ShouldReadRosterState(false, x) is always true: every GREEN spell re-reads as before.
set(lazy "if (ai::trainable_spells::ShouldReadRosterState(trainablePrecheck, rosterStateRead))")
require_count("${calc}" "${lazy}" 1 "lazy read gate")
require_order("${calc}" "if (state != TRAINER_SPELL_GREEN)" "${lazy}" "roster read only for GREEN spells")
require_order("${calc}" "${lazy}" "persistentRosterBot = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow());" "roster flag inside the gate")
require_order("${calc}" "persistentRosterBot = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow());" "pair = persistentRosterBot ? sRandomPlayerbotMgr.GetProfessionPair(bot->GetGUIDLow()) : 0;" "pair after flag")
require_order("${calc}" "pair = persistentRosterBot ? sRandomPlayerbotMgr.GetProfessionPair(bot->GetGUIDLow()) : 0;" "rosterStateRead = true;" "mark read")
require_order("${calc}" "rosterStateRead = true;" "profession_training::IsEligibleProfessionTraining(" "roster state before the eligibility")
require_count("${calc}" "IsPersistentRosterMember(" 1 "one roster read site")
require_count("${calc}" "GetProfessionPair(" 1 "one pair read site")
require_count("${calc}" "persistentRosterBot && !allowedRosterProfession" 1 "fail-closed roster gate kept")
require_count("${calc}" "trainableSpells.push_back(trainerSpell);" 1 "single accept site")

# 5. Switch: default off, documented with value 0, after the #541 switches.
require_text("${config_h}" "bool perfTrainableSpellsPrecheck = false;" "switch member default off")
require_order("${config_h}" "uint32 aiDelayJitterPct = 0;" "bool perfTrainableSpellsPrecheck = false;" "switch after the #541 switches")
require_text("${config_cpp}" "perfTrainableSpellsPrecheck = config.GetBoolDefault(\"AiPlayerbot.Perf.TrainableSpellsPrecheck\", false);" "switch read, default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.TrainableSpellsPrecheck = 0\n" "documented key, value 0")
forbid_text("${conf_dist}" "\nAiPlayerbot.Perf.TrainableSpellsPrecheck = 1" "documented key switched on")

message(STATUS "trainable_spells_precheck source contract passed")
