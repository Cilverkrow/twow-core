if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/TrainerAction.cpp" trainer)
require_text("${trainer}"
  "if (creature->GetCreatureInfo()->TrainerType == TRAINER_TYPE_TRADESKILLS)"
  "generic trainer tradeskill admission guard")
require_text("${trainer}"
  "// handled by the existing generic path."
  "generic trainer scope note")

file(READ "${PB_SOURCE_DIR}/strategy/actions/AutoLearnSpellAction.cpp" auto_learn)
require_text("${auto_learn}"
  "// tradeskill trainer bypass."
  "autolearn tradeskill scope note")
require_text("${auto_learn}"
  "if (co->TrainerType == TRAINER_TYPE_TRADESKILLS)"
  "autolearn tradeskill admission guard")

file(READ "${PB_SOURCE_DIR}/PlayerbotFactory.cpp" factory)
string(FIND "${factory}" "void PlayerbotFactory::InitTradeSkills()" factory_start)
string(FIND "${factory}" "void PlayerbotFactory::InitSkills()" factory_end)
if(factory_start EQUAL -1 OR factory_end EQUAL -1 OR factory_end LESS factory_start)
  message(FATAL_ERROR "Could not isolate the factory profession-planning region")
endif()
math(EXPR factory_length "${factory_end} - ${factory_start}")
string(SUBSTRING "${factory}" ${factory_start} ${factory_length} factory_region)
foreach(forbidden "SetRandomSkill(" "learnSpell(" "CastSpell(" "SetSkill(")
  string(FIND "${factory_region}" "${forbidden}" forbidden_offset)
  if(NOT forbidden_offset EQUAL -1)
    message(FATAL_ERROR "Factory profession-planning region still contains ${forbidden}")
  endif()
endforeach()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" manager)
require_text("${manager}" "std::numeric_limits<uint32>::max()"
  "non-expiring profession_pair event")
require_text("${manager}" "ai::profession::kEventData"
  "versioned profession_pair event data")

message(STATUS "PROFESSION_PAIR_SOURCE_CONTRACT=PASS")
