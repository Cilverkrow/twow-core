cmake_policy(VERSION 3.16)
# AUDIT-L03 B7 (R2 bug fix, no switch): TrainableSpellMapValue computes the bucket key of every trainer spell
# (TrainerSpellKeyPolicy.h) before the dedup loop reads (*spellMap)[trainerType][spellRequirement]. Pins the policy, the
# per-spell key before the dedup read, and forbids the old uninitialised declaration and the late tradeskill assignment.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "trainer spell key: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "trainer spell key: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "trainer spell key: forbidden ${description}: ${needle}")
  endif()
endfunction()

read_source("TrainerSpellKeyPolicy.h" policy)
require_text("${policy}" "inline std::uint32_t Requirement(std::uint32_t trainerType, std::uint32_t trainerClass, std::uint32_t trainerRace," "the policy function")
require_text("${policy}" "return reqSkill ? reqSkill : learnedSkill;" "the tradeskill key (reqSkill, else learned skill)")
require_text("${policy}" "        default:
            return 0;" "a defined key for unknown types")

read_source("strategy/values/TrainerValues.cpp" tv)
require_text("${tv}" "#include \"playerbot/TrainerSpellKeyPolicy.h\"" "the policy include")
require_text("${tv}" "ai::trainer_spell_key::TypeTradeskills == TRAINER_TYPE_TRADESKILLS" "the TrainerType mirror assert")
forbid_text("${tv}" "uint32 spellRequirement;" "the uninitialised key declaration")
forbid_text("${tv}" "spellRequirement = trainerSpell.reqSkill;" "the late tradeskill key assignment")
forbid_text("${tv}" "spellRequirement = spell->EffectMiscValue[1];" "the late tradeskill key assignment")

set(key "const uint32 spellRequirement = ai::trainer_spell_key::Requirement(trainerType, firstTrainer->TrainerClass,")
set(dedup "for (auto& [otherTrainerSpell, trainers] : (*spellMap)[trainerType][spellRequirement])")
set(file_in "(*spellMap)[trainerType][spellRequirement][sameTrainerSpell].push_back(trainer->Entry);")
require_text("${tv}" "${key}" "the per-spell key")
require_text("${tv}" "${dedup}" "the dedup read")
require_text("${tv}" "${file_in}" "the bucket insert")
string(FIND "${tv}" "for (auto& [id, trainerSpell] : trainer_spells->spellList)" loop_at)
string(FIND "${tv}" "${key}" key_at)
string(FIND "${tv}" "${dedup}" dedup_at)
string(FIND "${tv}" "${file_in}" insert_at)
if(NOT loop_at LESS key_at OR NOT key_at LESS dedup_at OR NOT dedup_at LESS insert_at)
  message(FATAL_ERROR "trainer spell key: order must be spell loop -> key -> dedup read -> insert")
endif()

message(STATUS "TRAINER_SPELL_KEY_CONTRACT=PASS")
