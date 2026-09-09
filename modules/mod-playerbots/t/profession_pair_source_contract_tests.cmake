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
file(READ "${PB_SOURCE_DIR}/PlayerbotFactory.h" factory_header)
require_text("${factory_header}"
  "void EnsureProfessionPairPlan();"
  "narrow plan-only factory entry point")
require_text("${factory}"
  "void PlayerbotFactory::EnsureProfessionPairPlan()"
  "plan-only factory implementation")
require_text("${factory}"
  "InitTradeSkills();"
  "existing profession-plan selection reuse")
string(FIND "${factory}" "void PlayerbotFactory::EnsureProfessionPairPlan()" ensure_start)
string(FIND "${factory}" "void PlayerbotFactory::InitTradeSkills()" ensure_end)
if(ensure_start EQUAL -1 OR ensure_end EQUAL -1 OR ensure_end LESS ensure_start)
  message(FATAL_ERROR "Could not isolate the profession-plan backfill wrapper")
endif()
math(EXPR ensure_length "${ensure_end} - ${ensure_start}")
string(SUBSTRING "${factory}" ${ensure_start} ${ensure_length} ensure_region)
foreach(forbidden "Randomize(" "InitAllSkills(" "SetSkill(" "learnSpell(" "CastSpell(" "AddItem(" "DestroyItem(" "SetMoney(" "CompleteQuest(" "RewardQuest(" "SetLevel(" "ResetTalents(")
  string(FIND "${ensure_region}" "${forbidden}" forbidden_offset)
  if(NOT forbidden_offset EQUAL -1)
    message(FATAL_ERROR "Profession-plan backfill wrapper contains ${forbidden}")
  endif()
endforeach()
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
file(READ "${PB_SOURCE_DIR}/PlayerbotMgr.cpp" holder)
require_text("${manager}" "std::numeric_limits<uint32>::max()"
  "non-expiring profession_pair event")
require_text("${manager}" "ai::profession::kEventData"
  "versioned profession_pair event data")
require_text("${manager}"
  "PlayerbotFactory(bot, bot->GetLevel()).EnsureProfessionPairPlan();"
  "persistent-roster login backfill")
require_text("${holder}"
  "CreateBotAI(bot);"
  "AI creation before login callback")
require_text("${holder}"
  "OnBotLoginInternal(bot);"
  "one-time successful bot-login callback")
string(FIND "${manager}" "void RandomPlayerbotMgr::OnBotLoginInternal(Player * const bot)" login_hook_start)
string(FIND "${manager}" "void RandomPlayerbotMgr::OnPlayerLogin(Player* player)" login_hook_end)
string(FIND "${manager}" "PlayerbotFactory(bot, bot->GetLevel()).EnsureProfessionPairPlan();" backfill_offset)
if(login_hook_start EQUAL -1 OR login_hook_end EQUAL -1 OR backfill_offset EQUAL -1 OR
   backfill_offset LESS login_hook_start OR backfill_offset GREATER login_hook_end)
  message(FATAL_ERROR "Profession-pair backfill is not confined to the one-time login callback")
endif()
string(FIND "${manager}" "bool RandomPlayerbotMgr::ProcessBot(uint32 bot)" process_start)
string(FIND "${manager}" "bool RandomPlayerbotMgr::ProcessBot(Player* player)" process_end)
if(process_start EQUAL -1 OR process_end EQUAL -1)
  message(FATAL_ERROR "Could not isolate the periodic ProcessBot path")
endif()
math(EXPR process_length "${process_end} - ${process_start}")
string(SUBSTRING "${manager}" ${process_start} ${process_length} process_region)
string(FIND "${process_region}" "EnsureProfessionPairPlan" process_backfill_offset)
if(NOT process_backfill_offset EQUAL -1)
  message(FATAL_ERROR "Profession-pair backfill must not run from periodic ProcessBot")
endif()

message(STATUS "PROFESSION_PAIR_SOURCE_CONTRACT=PASS")
