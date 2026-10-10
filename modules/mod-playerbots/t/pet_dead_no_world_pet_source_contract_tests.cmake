# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A07, card 28): behind AiPlayerbot.PetDeadNoWorldPetFalse (default 0 = as before) the
# hunter value "pet dead" is false when the bot has no pet in the world. Revive Pet can only revive a dead pet
# in the world (core CheckCast SUMMON_DEAD_PET -> SPELL_FAILED_NO_PET), so the synchronous character_pet
# SELECT and the doomed revive attempt (incl. its CastSpell stand-up / self-select and the stale-basket
# dismount) are skipped. Approved as BEHAVIOUR-CHANGING. A dead pet in the world still makes the value true;
# a pet that died and was unsummoned comes back dead through Call Pet (pet cache keeps curhealth 0), so the
# unchanged last line still lets Revive Pet work for it.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "CORE_SOURCE_DIR is required")
endif()

function(read_file path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "#541 A07: missing file ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A07: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A07: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A07: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A07: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A07: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle out)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" off)
    if(off EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR off "${off} + ${len}")
    string(SUBSTRING "${rest}" ${off} -1 rest)
  endwhile()
  set(${out} ${n} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" n)
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#541 A07: ${description}: '${needle}' found ${n} times, expected ${expected}")
  endif()
endfunction()

read_file("${PB_SOURCE_DIR}/strategy/values/StatsValues.cpp" stats_cpp)
read_file("${PB_SOURCE_DIR}/strategy/values/StatsValues.h" stats_h)
read_file("${PB_SOURCE_DIR}/strategy/values/ValueContext.h" value_context_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf_dist)
read_file("${PB_SOURCE_DIR}/strategy/hunter/HunterTriggers.cpp" hunter_triggers)
read_file("${PB_SOURCE_DIR}/strategy/hunter/HunterStrategy.cpp" hunter_strategy)
read_file("${PB_SOURCE_DIR}/strategy/hunter/HunterActions.h" hunter_actions)
read_file("${CORE_SOURCE_DIR}/src/game/Spells/Spell.cpp" spell_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Spells/SpellEffects.cpp" spell_effects_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/Pet.cpp" pet_cpp)

# 1) Switch: own key, default off in code and in the shipped conf.
require_text("${config_h}" "bool petDeadNoWorldPetFalse = false;" "member default off")
require_text("${config_cpp}" "petDeadNoWorldPetFalse = config.GetBoolDefault(\"AiPlayerbot.PetDeadNoWorldPetFalse\", false);" "config default 0")
require_count("${config_cpp}" "AiPlayerbot.PetDeadNoWorldPetFalse" 1 "key read once")
require_text("${conf_dist}" "\nAiPlayerbot.PetDeadNoWorldPetFalse = 0\n" "documented key = 0")
forbid_text("${conf_dist}" "AiPlayerbot.PetDeadNoWorldPetFalse = 1" "conf default on")

# 2) The gate: inside the no-world-pet branch, before the H4 cache and its SELECT; legacy and dead-world-pet kept.
region("${stats_cpp}" "bool PetIsDeadValue::Calculate()" "bool PetIsHappyValue::Calculate()" body)
set(gate "        if (sPlayerbotAIConfig.petDeadNoWorldPetFalse)\n            return false;\n")
set(select "CharacterDatabase.PQuery(\"SELECT id FROM character_pet WHERE owner = '%u'\", ownerid);")
set(final_return "    return bot->GetPet() && sServerFacade.GetDeathState(bot->GetPet()) != ALIVE;\n}")
require_text("${body}" "${gate}" "gated early false")
require_count("${body}" "petDeadNoWorldPetFalse" 1 "switch read once")
require_count("${body}" "return false;" 1 "only the gated early false")
require_order("${body}" "    if (!bot->GetPet())\n    {\n" "${gate}" "gate inside the no-world-pet branch")
require_order("${body}" "${gate}" "time_t const now = time(nullptr);" "gate before the H4 cache")
require_order("${body}" "${gate}" "${select}" "gate before the SELECT")
require_order("${body}" "${select}" "        return hasStoredPet;\n    }" "H4 legacy path kept (switch off)")
require_order("${body}" "        return hasStoredPet;\n    }" "${final_return}" "dead world pet check kept last")
require_count("${body}" "${select}" 1 "single SELECT")
# Only the switch decides; no extra world-state predicate (approved: unconditional for no world pet).
foreach(extra IN ITEMS "IsStandingUp()" "IsMounted()" "HasOption(" "IsInCombat()")
  forbid_text("${body}" "${extra}" "extra predicate in the A07 gate (approved scope is no world pet only)")
endforeach()

# 3) Single consumer: "pet dead" appears exactly once in each of its three files and nowhere else
#    (declaration default name, creator, hunter trigger). New comments must not quote the value name.
require_count("${stats_h}" "\"pet dead\"" 1 "value name in StatsValues.h")
require_count("${value_context_h}" "creators[\"pet dead\"]" 1 "value creator")
require_text("${hunter_triggers}" "return AI_VALUE(bool, \"pet dead\") && !AI_VALUE2(bool, \"mounted\", \"self target\");" "single consumer (trigger)")
file(GLOB_RECURSE pb_sources "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(total 0)
foreach(f IN LISTS pb_sources)
  file(READ "${f}" t)
  count_text("${t}" "\"pet dead\"" n)
  math(EXPR total "${total} + ${n}")
endforeach()
if(NOT total EQUAL 3)
  message(FATAL_ERROR "#541 A07: \"pet dead\" occurs ${total} times in the module, expected 3 (StatsValues.h, ValueContext.h, HunterTriggers.cpp) - a new consumer must be re-checked")
endif()

# 4) The trigger feeds only "revive pet", which is a plain buff cast without own isUseful/isPossible.
require_text("${hunter_strategy}" "\"hunters pet dead\",\n        NextAction::array(0, new NextAction(\"revive pet\", ACTION_NORMAL + 1), NULL)));" "trigger -> revive pet only")
require_count("${hunter_strategy}" "\"hunters pet dead\"" 1 "single trigger node")
require_text("${hunter_actions}" "class CastRevivePetAction : public CastBuffSpellAction\n    {\n    public:\n        CastRevivePetAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, \"revive pet\") {}\n    };" "revive pet action unchanged")

# 5) Core premise: Revive Pet needs a pet in the world (CheckCast and the effect handler).
region("${spell_cpp}" "SpellCastResult Spell::CheckCast(bool strict)" "SpellCastResult Spell::CheckCasterAuras() const" check_cast)
region("${check_cast}" "case SPELL_EFFECT_SUMMON_DEAD_PET:" "break;" dead_pet)
require_text("${dead_pet}" "Creature *pet = m_casterUnit ? m_casterUnit->GetPet() : nullptr;" "revive looks at the world pet")
require_text("${dead_pet}" "if (!pet)\n                    return SPELL_FAILED_NO_PET;" "no world pet -> NO_PET")
region("${spell_effects_cpp}" "void Spell::EffectSummonDeadPet(" "if (damage < 0)" effect)
require_text("${effect}" "Pet *pet = _player->GetPet();\n    if (!pet)\n        return;" "effect is a no-op without a world pet")

# 6) Core premise (reviewer): a pet that died and was unsummoned keeps curhealth 0 in the runtime pet cache
#    (only the SQL INSERT clamps to 1), and LoadPetFromDB sets it JUST_DIED, so Call Pet brings it back dead
#    into the world, where the unchanged last line makes the value true and Revive Pet works.
require_text("${pet_cpp}" "m_pTmpCache->curhealth = curhealth;" "pet cache keeps the raw health")
require_text("${pet_cpp}" "uint32 savedhealth = m_pTmpCache->curhealth;" "pet load reads the cached health")
require_text("${pet_cpp}" "if ((getPetType() != SUMMON_PET || current) && !savedhealth)\n        SetDeathState(JUST_DIED);" "a dead pet loads dead")

message(STATUS "pet_dead_no_world_pet source contract passed")
